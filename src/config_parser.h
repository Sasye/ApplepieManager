#pragma once
// ============================================================================
// config_parser.h — Generic key=value config file reader/writer
// ============================================================================
// Preserves comments, blank lines, and ordering when writing back.
// Supports [section] headers for grouping.
// ============================================================================

#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

// ============================================================================
// Data Structures
// ============================================================================

enum ConfigLineType {
    CFG_BLANK,      // empty line
    CFG_COMMENT,    // # or ; comment
    CFG_SECTION,    // [section_name]
    CFG_KEYVALUE    // key=value
};

struct ConfigLine {
    ConfigLineType type;
    std::string raw;        // original line text (for comments/blank)
    std::string section;    // current section context
    std::string key;        // key (for CFG_KEYVALUE)
    std::string value;      // value (for CFG_KEYVALUE)
};

struct ConfigFile {
    std::vector<ConfigLine> lines;
    std::string filePath;
    bool loaded;
};

// ============================================================================
// Parsing
// ============================================================================

static std::string TrimWhitespace(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static ConfigFile LoadConfigFile(const char* path) {
    ConfigFile cfg;
    cfg.filePath = path;
    cfg.loaded = false;

    FILE* f = fopen(path, "r");
    if (!f) return cfg;

    char buf[1024];
    std::string currentSection;

    while (fgets(buf, sizeof(buf), f)) {
        // Remove trailing newline
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';

        ConfigLine line;
        line.raw = buf;
        line.section = currentSection;

        std::string trimmed = TrimWhitespace(buf);

        if (trimmed.empty()) {
            line.type = CFG_BLANK;
        } else if (trimmed[0] == '#' || trimmed[0] == ';') {
            line.type = CFG_COMMENT;
        } else if (trimmed[0] == '[' && trimmed.back() == ']') {
            line.type = CFG_SECTION;
            currentSection = trimmed.substr(1, trimmed.size() - 2);
            line.section = currentSection;
        } else {
            size_t eq = trimmed.find('=');
            if (eq != std::string::npos) {
                line.type = CFG_KEYVALUE;
                line.key = TrimWhitespace(trimmed.substr(0, eq));
                line.value = TrimWhitespace(trimmed.substr(eq + 1));
            } else {
                line.type = CFG_COMMENT; // treat malformed lines as comments
            }
        }

        cfg.lines.push_back(line);
    }

    fclose(f);
    cfg.loaded = true;
    return cfg;
}

// ============================================================================
// Querying
// ============================================================================

static const char* ConfigGetValue(const ConfigFile& cfg, const char* key,
                                  const char* section = nullptr) {
    for (const auto& line : cfg.lines) {
        if (line.type != CFG_KEYVALUE) continue;
        if (section && line.section != section) continue;
        if (line.key == key) return line.value.c_str();
    }
    return nullptr;
}

static int ConfigGetInt(const ConfigFile& cfg, const char* key, int defaultVal,
                        const char* section = nullptr) {
    const char* v = ConfigGetValue(cfg, key, section);
    if (!v) return defaultVal;
    return atoi(v);
}

static bool ConfigGetBool(const ConfigFile& cfg, const char* key, bool defaultVal,
                          const char* section = nullptr) {
    const char* v = ConfigGetValue(cfg, key, section);
    if (!v) return defaultVal;
    return (strcmp(v, "1") == 0 || _stricmp(v, "true") == 0);
}

// ============================================================================
// Modification (preserves file structure)
// ============================================================================

static bool ConfigSetValue(ConfigFile& cfg, const char* key, const char* value,
                           const char* section = nullptr) {
    for (auto& line : cfg.lines) {
        if (line.type != CFG_KEYVALUE) continue;
        if (section && line.section != section) continue;
        if (line.key == key) {
            line.value = value;
            return true; // modified existing
        }
    }

    // Key not found — append to the appropriate section
    ConfigLine newLine;
    newLine.type = CFG_KEYVALUE;
    newLine.key = key;
    newLine.value = value;
    newLine.section = section ? section : "";

    if (section) {
        // Find the last line in this section and insert after it
        int insertIdx = -1;
        for (int i = (int)cfg.lines.size() - 1; i >= 0; i--) {
            if (cfg.lines[i].section == section) {
                insertIdx = i + 1;
                break;
            }
        }
        if (insertIdx >= 0) {
            cfg.lines.insert(cfg.lines.begin() + insertIdx, newLine);
            return true;
        }
        // Section not found — create it
        ConfigLine sectionLine;
        sectionLine.type = CFG_SECTION;
        sectionLine.section = section;
        sectionLine.raw = std::string("[") + section + "]";
        cfg.lines.push_back(sectionLine);
    }

    cfg.lines.push_back(newLine);
    return true;
}

// ============================================================================
// Writing (preserves comments/blanks/ordering)
// ============================================================================

static bool SaveConfigFile(const ConfigFile& cfg) {
    FILE* f = fopen(cfg.filePath.c_str(), "w");
    if (!f) return false;

    for (const auto& line : cfg.lines) {
        switch (line.type) {
            case CFG_BLANK:
                fprintf(f, "\n");
                break;
            case CFG_COMMENT:
                fprintf(f, "%s\n", line.raw.c_str());
                break;
            case CFG_SECTION:
                fprintf(f, "[%s]\n", line.section.c_str());
                break;
            case CFG_KEYVALUE:
                fprintf(f, "%s=%s\n", line.key.c_str(), line.value.c_str());
                break;
        }
    }

    fclose(f);
    return true;
}

// ============================================================================
// Virtual Key Code <-> Name Conversion
// ============================================================================

struct VKNameEntry {
    int vk;
    const char* name;
};

static const VKNameEntry g_vkNames[] = {
    // Function keys
    {VK_F1, "F1"}, {VK_F2, "F2"}, {VK_F3, "F3"}, {VK_F4, "F4"},
    {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"}, {VK_F8, "F8"},
    {VK_F9, "F9"}, {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"},

    // Navigation
    {VK_INSERT, "INSERT"}, {VK_DELETE, "DELETE"}, {VK_HOME, "HOME"},
    {VK_END, "END"}, {VK_PRIOR, "PAGEUP"}, {VK_NEXT, "PAGEDOWN"},

    // Arrow keys
    {VK_LEFT, "LEFT"}, {VK_RIGHT, "RIGHT"}, {VK_UP, "UP"}, {VK_DOWN, "DOWN"},

    // Modifiers
    {VK_SHIFT, "SHIFT"}, {VK_CONTROL, "CTRL"}, {VK_MENU, "ALT"},
    {VK_LSHIFT, "LSHIFT"}, {VK_RSHIFT, "RSHIFT"},
    {VK_LCONTROL, "LCTRL"}, {VK_RCONTROL, "RCTRL"},
    {VK_LMENU, "LALT"}, {VK_RMENU, "RALT"},

    // Special
    {VK_SPACE, "SPACE"}, {VK_RETURN, "ENTER"}, {VK_ESCAPE, "ESC"},
    {VK_TAB, "TAB"}, {VK_BACK, "BACKSPACE"}, {VK_CAPITAL, "CAPSLOCK"},

    // Numpad
    {VK_NUMPAD0, "NUMPAD0"}, {VK_NUMPAD1, "NUMPAD1"},
    {VK_NUMPAD2, "NUMPAD2"}, {VK_NUMPAD3, "NUMPAD3"},
    {VK_NUMPAD4, "NUMPAD4"}, {VK_NUMPAD5, "NUMPAD5"},
    {VK_NUMPAD6, "NUMPAD6"}, {VK_NUMPAD7, "NUMPAD7"},
    {VK_NUMPAD8, "NUMPAD8"}, {VK_NUMPAD9, "NUMPAD9"},
    {VK_MULTIPLY, "NUMPAD*"}, {VK_ADD, "NUMPAD+"},
    {VK_SUBTRACT, "NUMPAD-"}, {VK_DECIMAL, "NUMPAD."},
    {VK_DIVIDE, "NUMPAD/"},

    // Mouse
    {VK_LBUTTON, "LMOUSE"}, {VK_RBUTTON, "RMOUSE"}, {VK_MBUTTON, "MMOUSE"},

    {0, nullptr} // sentinel
};

static const char* VKToName(int vk) {
    // Check named keys
    for (int i = 0; g_vkNames[i].name; i++) {
        if (g_vkNames[i].vk == vk)
            return g_vkNames[i].name;
    }
    // Printable ASCII
    static char buf[8];
    if (vk >= '0' && vk <= '9') {
        buf[0] = (char)vk; buf[1] = 0;
        return buf;
    }
    if (vk >= 'A' && vk <= 'Z') {
        buf[0] = (char)vk; buf[1] = 0;
        return buf;
    }
    // Unknown
    snprintf(buf, sizeof(buf), "VK_%02X", vk);
    return buf;
}

static int NameToVK(const char* name) {
    if (!name || !name[0]) return 0;

    // Check named keys (case insensitive)
    for (int i = 0; g_vkNames[i].name; i++) {
        if (_stricmp(g_vkNames[i].name, name) == 0)
            return g_vkNames[i].vk;
    }
    // Single character
    if (name[1] == '\0') {
        char c = name[0];
        if (c >= '0' && c <= '9') return c;
        if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
        if (c >= 'A' && c <= 'Z') return c;
    }
    // VK_XX hex format
    if (_strnicmp(name, "VK_", 3) == 0) {
        return (int)strtol(name + 3, nullptr, 16);
    }
    return 0;
}

// ============================================================================
// Plugin-specific config file read/write helpers
// ============================================================================

// Read a plugin's config file for display in the manager GUI
// Returns entries only from the root section (no [section] grouping)
struct PluginConfigEntry {
    std::string key;
    std::string value;
    std::string comment; // preceding comment line, if any
};

static std::vector<PluginConfigEntry> ReadPluginConfig(const char* path) {
    std::vector<PluginConfigEntry> entries;
    ConfigFile cfg = LoadConfigFile(path);
    if (!cfg.loaded) return entries;

    std::string pendingComment;
    for (const auto& line : cfg.lines) {
        if (line.type == CFG_COMMENT) {
            pendingComment = line.raw;
        } else if (line.type == CFG_KEYVALUE) {
            PluginConfigEntry e;
            e.key = line.key;
            e.value = line.value;
            e.comment = pendingComment;
            entries.push_back(e);
            pendingComment.clear();
        } else {
            pendingComment.clear();
        }
    }
    return entries;
}

// Update a single key in a plugin's config file (preserves structure)
static bool UpdatePluginConfigValue(const char* path, const char* key, const char* value) {
    ConfigFile cfg = LoadConfigFile(path);
    if (!cfg.loaded) return false;
    ConfigSetValue(cfg, key, value);
    return SaveConfigFile(cfg);
}
