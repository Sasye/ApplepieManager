#pragma once
#include <windows.h>
#include <climits>
#include <cstdint>
#include <map>
#include <string>

// Shared UTF-8 storage for basic plugin settings and Manager preferences.
namespace config_file {
using Values = std::map<std::string, std::string>;
inline volatile LONG serial = 0;
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
inline bool missing(DWORD error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }
inline bool text(const std::string& value) {
    for (unsigned char c : value) if (c < 32 || c == 127) return false;
    return value.empty() || (value.size() <= INT_MAX &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0));
}
inline DWORD read(const std::wstring& path, std::string& contents) {
    File file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (file.handle == INVALID_HANDLE_VALUE) return GetLastError();
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.handle, &size)) return GetLastError();
    if (size.QuadPart < 0 || uint64_t(size.QuadPart) > contents.max_size()) return ERROR_FILE_TOO_LARGE;
    contents.assign(static_cast<size_t>(size.QuadPart), '\0');
    for (size_t offset = 0; offset < contents.size();) {
        const DWORD chunk = DWORD((contents.size() - offset) > MAXDWORD ? MAXDWORD : contents.size() - offset);
        DWORD count = 0;
        if (!ReadFile(file.handle, contents.data() + offset, chunk, &count, nullptr)) return GetLastError();
        if (count != chunk) return ERROR_READ_FAULT;
        offset += count;
    }
    return ERROR_SUCCESS;
}
inline DWORD parse(const std::string& contents, Values& values) {
    Values parsed;
    size_t start = contents.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    while (start < contents.size()) {
        auto end = contents.find('\n', start);
        if (end == std::string::npos) end = contents.size();
        auto line = contents.substr(start, end - start); start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > INT_MAX || line.find('\0') != std::string::npos || (!line.empty() &&
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(), int(line.size()), nullptr, 0))) return ERROR_INVALID_DATA;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || !eq) return ERROR_INVALID_DATA;
        auto name = line.substr(0, eq), value = line.substr(eq + 1);
        if (!text(name) || !text(value) || name.front() == ' ' || name.back() == ' ' ||
            !parsed.emplace(name, value).second) return ERROR_INVALID_DATA;
    }
    values.swap(parsed);
    return ERROR_SUCCESS;
}
inline std::string encode(const Values& values) {
    std::string contents = "# Applepie basic configuration v1 (UTF-8)\n";
    for (const auto& item : values) contents += item.first + "=" + item.second + "\n";
    return contents;
}
inline DWORD write(const std::wstring& directory, const std::wstring& destination,
                   const std::string& contents, bool replace = true) {
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return GetLastError();
    const auto path = destination + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
        std::to_wstring(InterlockedIncrement(&serial));
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD error = ERROR_SUCCESS;
    for (size_t offset = 0; offset < contents.size() && !error;) {
        const DWORD chunk = DWORD((contents.size() - offset) > MAXDWORD ? MAXDWORD : contents.size() - offset);
        DWORD written = 0;
        if (!WriteFile(file, contents.data() + offset, chunk, &written, nullptr)) error = GetLastError();
        else if (written != chunk) error = ERROR_WRITE_FAULT;
        offset += written;
    }
    if (!error && !FlushFileBuffers(file)) error = GetLastError();
    if (!CloseHandle(file) && !error) error = GetLastError();
    if (!error && !MoveFileExW(path.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH |
        (replace ? MOVEFILE_REPLACE_EXISTING : 0))) error = GetLastError();
    if (error) DeleteFileW(path.c_str()); // Only this call's temporary file.
    return error;
}
}
