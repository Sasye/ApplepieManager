# Applepie Manager 对接指南

本文档说明如何将插件与 Applepie Manager 对接。

## 快速开始

### 1. 复制接口头文件

将 `src/applepie_mgr.h` 复制到你的插件项目的 `src/` 目录下。

### 2. 实现必需的导出函数

在你的插件主源文件中：

```cpp
#define APPLEPIE_PLUGIN_IMPL
#include "applepie_mgr.h"

// 静态插件信息（在 DLL 生命周期内必须有效）
static AP_PluginInfo s_pluginInfo = {
    APPLEPIE_PLUGIN_API_VERSION,
    "my_plugin",                    // 唯一标识符
    "My Plugin 我的插件",            // 显示名称
    "A brief description",          // 描述
    "my_plugin_config.txt",         // 配置文件名（相对于 plugin\），无配置则 nullptr
    true                            // 是否支持运行时暂停/恢复
};

// [必需] 返回插件信息
APPLEPIE_PLUGIN_EXPORT AP_PluginInfo* AP_GetPluginInfo() {
    return &s_pluginInfo;
}
```

### 3. 编译为 DLL

编译输出的 DLL 放入游戏目录的 `plugin\` 文件夹中，管理器会自动扫描并加载。

## 导出函数一览

| 函数 | 必需 | 说明 |
|------|:----:|------|
| `AP_GetPluginInfo` | ✅ | 返回 `AP_PluginInfo*`，管理器用于识别插件 |
| `AP_PluginEnable` | ❌ | 运行时恢复插件功能，返回 `true` 表示成功 |
| `AP_PluginDisable` | ❌ | 运行时暂停插件功能（hook 直通），返回 `true` 表示成功 |
| `AP_ReloadConfig` | ❌ | 热重载配置文件并应用更改，返回 `true` 表示成功 |
| `AP_GetHotkeys` | ❌ | 填充热键信息数组，返回实际数量 |
| `AP_SetLanguage` | ❌ | 接收语言代码（`"zh"`、`"en"` 等），管理器切换语言时调用 |

> 只有 `AP_GetPluginInfo` 是必需的。其他函数管理器会通过 `GetProcAddress` 查找，未实现则忽略。

## 数据结构

### AP_PluginInfo

```cpp
struct AP_PluginInfo {
    int         apiVersion;         // 必须为 APPLEPIE_PLUGIN_API_VERSION (1)
    const char* id;                 // 唯一标识符
    const char* displayName;        // 显示名称
    const char* description;        // 简短描述
    const char* configFile;         // 配置文件名（相对于 plugin\），或 nullptr
    bool        supportsHotDisable; // 是否支持运行时暂停/恢复
};
```

### AP_HotkeyInfo

```cpp
struct AP_HotkeyInfo {
    const char* name;               // 显示名称
    const char* configKey;          // 配置文件中的键名（nullptr = 不可配置）
    int         currentVK;          // 当前虚拟键码
};
```

## 完整示例

以下是一个带热键和配置支持的完整插件模板：

```cpp
#define APPLEPIE_PLUGIN_IMPL
#include "applepie_mgr.h"
#include <windows.h>

static bool g_enabled = true;

// 热键信息
static AP_HotkeyInfo s_hotkeys[] = {
    { "Toggle Feature", "toggle_key", VK_F5 },
};

static AP_PluginInfo s_pluginInfo = {
    APPLEPIE_PLUGIN_API_VERSION,
    "example_plugin",
    "Example Plugin",
    "An example plugin template",
    "example_config.txt",
    true
};

APPLEPIE_PLUGIN_EXPORT AP_PluginInfo* AP_GetPluginInfo() {
    return &s_pluginInfo;
}

APPLEPIE_PLUGIN_EXPORT bool AP_PluginEnable() {
    g_enabled = true;
    return true;
}

APPLEPIE_PLUGIN_EXPORT bool AP_PluginDisable() {
    g_enabled = false;
    return true;
}

APPLEPIE_PLUGIN_EXPORT bool AP_ReloadConfig() {
    // 重新读取配置文件并应用
    return true;
}

APPLEPIE_PLUGIN_EXPORT int AP_GetHotkeys(AP_HotkeyInfo* outArray, int maxCount) {
    int count = sizeof(s_hotkeys) / sizeof(s_hotkeys[0]);
    if (count > maxCount) count = maxCount;
    for (int i = 0; i < count; i++) outArray[i] = s_hotkeys[i];
    return count;
}

APPLEPIE_PLUGIN_EXPORT void AP_SetLanguage(const char* langCode) {
    // 根据 langCode ("zh" / "en") 切换插件界面语言
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // 插件初始化逻辑
    }
    return TRUE;
}
```

## 热键重绑定

如果插件实现了 `AP_GetHotkeys` 和 `AP_ReloadConfig`，管理器会：

1. 在热键总览中显示该插件的所有热键
2. 检测与其他插件的热键冲突
3. 允许用户通过 GUI 重绑定热键
4. 重绑定时自动更新配置文件中对应的 `configKey` 并调用 `AP_ReloadConfig`

配置文件格式为 `key=value`（每行一个），管理器会直接修改指定的 `configKey` 对应的行。

## 语言同步

管理器切换语言时会对所有实现了 `AP_SetLanguage` 的插件调用该函数。`langCode` 参数为小写字符串：

- `"zh"` — 中文
- `"en"` — 英文

插件内部可以用任意方式实现多语言，只需通过此回调同步即可。管理器在插件加载完成后也会立即调用一次，确保初始语言同步。
