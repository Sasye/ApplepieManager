#pragma once
#include <windows.h>
#include <cstdio>
#include <cstring>

// One writer, four bounded files. Failed rotation never truncates the last log.
// No use from exception filters or Loader Lock.
class BoundedLog {
    SRWLOCK lock_ = SRWLOCK_INIT;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    wchar_t path_[MAX_PATH]{};
    DWORD limit_ = 1024 * 1024;
    uint64_t bytes_ = 0;
    bool blocked_ = false;
    bool rotate() {
        if (file_ != INVALID_HANDLE_VALUE) { CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
        wchar_t from[MAX_PATH + 8], to[MAX_PATH + 8];
        for (int i = 2; i >= 0; --i) {
            if (i) swprintf_s(from, L"%s.%d", path_, i); else wcscpy_s(from, path_);
            swprintf_s(to, L"%s.%d", path_, i + 1);
            if (!MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING) && GetLastError() != ERROR_FILE_NOT_FOUND)
                return false;
        }
        bytes_ = 0;
        return true;
    }
    bool reopen() {
        file_ = CreateFileW(path_, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER size{};
        if (file_ == INVALID_HANDLE_VALUE || !GetFileSizeEx(file_, &size)) return false;
        bytes_ = size.QuadPart;
        return true;
    }
public:
    BoundedLog() = default;
    BoundedLog(const BoundedLog&) = delete;
    BoundedLog& operator=(const BoundedLog&) = delete;
    ~BoundedLog() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
    bool open(const wchar_t* path, DWORD limit = 1024 * 1024) {
        AcquireSRWLockExclusive(&lock_);
        bool ok = false;
        __try {
            if (file_ != INVALID_HANDLE_VALUE || !path || wcslen(path) >= MAX_PATH || limit < 256) __leave;
            wcscpy_s(path_, path); limit_ = limit;
            ok = reopen();
            if (ok && bytes_) { ok = rotate(); if (ok) ok = reopen(); }
            blocked_ = !ok;
        } __finally { ReleaseSRWLockExclusive(&lock_); }
        return ok;
    }
    void write(const char* message) {
        char record[4608]; SYSTEMTIME now{}; GetLocalTime(&now);
        int prefix = sprintf_s(record, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
        _snprintf_s(record + prefix, sizeof(record) - prefix, _TRUNCATE, "%s", message);
        size_t n = strlen(record);
        if (n && record[n-1] != '\n' && n + 1 < sizeof(record)) { record[n++] = '\n'; record[n] = 0; }
        OutputDebugStringA(record);
        AcquireSRWLockExclusive(&lock_);
        __try {
            if (blocked_ || file_ == INVALID_HANDLE_VALUE || n > limit_) __leave;
            if (bytes_ + n > limit_ && (!rotate() || !reopen())) {
                blocked_ = true; OutputDebugStringA("[APH] log rotation failed; file logging stopped\n"); __leave;
            }
            DWORD written = 0;
            while (written < n) {
                DWORD part = 0;
                if (!WriteFile(file_, record + written, (DWORD)n - written, &part, nullptr) || !part) { blocked_ = true; break; }
                written += part; bytes_ += part;
            }
        } __finally { ReleaseSRWLockExclusive(&lock_); }
    }
};
