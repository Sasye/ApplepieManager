#pragma once
// ============================================================================
// il2cpp_text_scanner.h — IL2CPP runtime UID/name control
// ============================================================================
// Hooks TMP_Text.set_text to hide UID/name elements across different UI views:
//   - Watermark (in-game UID)
//   - Menu (ManagerNumber / ManagerName)
//   - Business card (PlayerUidTxt / NameTxt)
// Also provides SetActive-based hide for the in-game UID panel.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <windows.h>

// Forward decl
void Log(const char* fmt, ...);

// ============================================================================
// IL2CPP function pointer types
// ============================================================================

#define IL2CPP_DECL(ret, name, ...) \
    typedef ret (*t_##name)(__VA_ARGS__); \
    static t_##name p_##name = nullptr

IL2CPP_DECL(void*,  il2cpp_domain_get);
IL2CPP_DECL(void*,  il2cpp_thread_attach, void*);
IL2CPP_DECL(void**, il2cpp_domain_get_assemblies, void*, size_t*);
IL2CPP_DECL(void*,  il2cpp_assembly_get_image, void*);
IL2CPP_DECL(const char*, il2cpp_image_get_name, void*);
IL2CPP_DECL(void*,  il2cpp_class_from_name, void*, const char*, const char*);
IL2CPP_DECL(void*,  il2cpp_class_get_method_from_name, void*, const char*, int);
IL2CPP_DECL(void*,  il2cpp_runtime_invoke, void*, void*, void**, void**);
IL2CPP_DECL(void*,  il2cpp_type_get_object, void*);
IL2CPP_DECL(void*,  il2cpp_class_get_type, void*);
IL2CPP_DECL(void*,  il2cpp_string_new, const char*);

#undef IL2CPP_DECL

// ============================================================================
// State
// ============================================================================

static bool g_il2cppResolved = false;
static HMODULE g_hGameAssembly = nullptr;

// Cached method pointers
static void* g_methodComponentGetGameObject = nullptr;
static void* g_methodComponentGetTransform = nullptr;
static void* g_methodGameObjectSetActive = nullptr;
static void* g_methodTransformGetName = nullptr;
static void* g_methodTransformGetParent = nullptr;
static void* g_methodGameObjectFind = nullptr;

// TMP_Text class and methods
static void* g_classTMP_Text = nullptr;
static void* g_methodGetText = nullptr;
static void* g_methodSetText = nullptr;

// UID control state
static void* g_uidPanelGO = nullptr;
static void* g_uidTextGO = nullptr;
static void* g_uidPingGO = nullptr;
static bool  g_uidHidden = true;
static bool  g_pingHidden = false;
static bool  g_menuUidHidden = true;
static bool  g_menuNameHidden = true;
static bool  g_cardUidHidden = true;
static bool  g_cardNameHidden = true;

// ============================================================================
// IL2CPP String reading
// ============================================================================

static int ReadIL2CppString(void* str, char* buf, int bufSz) {
    __try {
        if (!str) { buf[0] = 0; return -1; }
        int32_t len = *(int32_t*)((char*)str + 16);
        if (len <= 0 || len > 2048) { buf[0] = 0; return -1; }
        wchar_t* chars = (wchar_t*)((char*)str + 20);
        int result = WideCharToMultiByte(CP_UTF8, 0, chars, len, buf, bufSz - 1, NULL, NULL);
        if (result <= 0) { buf[0] = 0; return -1; }
        buf[result] = 0;
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        buf[0] = 0;
        return -2;
    }
}

// ============================================================================
// Safe IL2CPP invoke wrapper
// ============================================================================

static void* SafeInvoke(void* method, void* obj, void** params = nullptr) {
    if (!method) return nullptr;
    __try {
        void* exc = nullptr;
        return p_il2cpp_runtime_invoke(method, obj, params, &exc);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ============================================================================
// IL2CPP Resolution
// ============================================================================

static void* FindClassInAssemblies(void** asms, size_t count,
                                    const char* ns, const char* name) {
    for (size_t i = 0; i < count; i++) {
        void* img = p_il2cpp_assembly_get_image(asms[i]);
        if (!img) continue;
        void* klass = p_il2cpp_class_from_name(img, ns, name);
        if (klass) return klass;
    }
    return nullptr;
}

static bool ResolveIL2Cpp() {
    g_hGameAssembly = GetModuleHandleW(L"GameAssembly.dll");
    if (!g_hGameAssembly) return false;

#define R(name) p_##name = (t_##name)GetProcAddress(g_hGameAssembly, #name)
    R(il2cpp_domain_get);
    R(il2cpp_thread_attach);
    R(il2cpp_domain_get_assemblies);
    R(il2cpp_assembly_get_image);
    R(il2cpp_image_get_name);
    R(il2cpp_class_from_name);
    R(il2cpp_class_get_method_from_name);
    R(il2cpp_runtime_invoke);
    R(il2cpp_type_get_object);
    R(il2cpp_class_get_type);
    R(il2cpp_string_new);
#undef R

    return p_il2cpp_domain_get && p_il2cpp_runtime_invoke &&
           p_il2cpp_class_from_name && p_il2cpp_class_get_method_from_name;
}

static bool InitIL2CppScanner() {
    for (int i = 0; i < 60; i++) {
        if (ResolveIL2Cpp()) {
            Log("[AM-IL2CPP] Resolved after %d seconds", i + 1);
            break;
        }
        Sleep(1000);
    }
    if (!g_hGameAssembly) {
        Log("[AM-IL2CPP] Failed to resolve GameAssembly.dll");
        return false;
    }

    void* dom = p_il2cpp_domain_get();
    if (!dom) { Log("[AM-IL2CPP] domain_get failed"); return false; }
    p_il2cpp_thread_attach(dom);

    size_t asmCount = 0;
    void** asms = p_il2cpp_domain_get_assemblies(dom, &asmCount);
    if (!asms || asmCount == 0) {
        Log("[AM-IL2CPP] No assemblies found");
        return false;
    }
    Log("[AM-IL2CPP] Found %d assemblies", (int)asmCount);

    // GameObject class
    void* goClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "GameObject");
    if (goClass) {
        g_methodGameObjectSetActive =
            p_il2cpp_class_get_method_from_name(goClass, "SetActive", 1);
        g_methodGameObjectFind =
            p_il2cpp_class_get_method_from_name(goClass, "Find", 1);
        Log("[AM-IL2CPP] GameObject.SetActive: %p, Find: %p",
            g_methodGameObjectSetActive, g_methodGameObjectFind);
    }

    // Component.get_gameObject
    void* compClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Component");
    if (compClass) {
        g_methodComponentGetGameObject =
            p_il2cpp_class_get_method_from_name(compClass, "get_gameObject", 0);
        g_methodComponentGetTransform =
            p_il2cpp_class_get_method_from_name(compClass, "get_transform", 0);
        Log("[AM-IL2CPP] Component.get_gameObject: %p, get_transform: %p",
            g_methodComponentGetGameObject, g_methodComponentGetTransform);
    }

    // Object.get_name (reused as transform name getter)
    void* objectClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Object");
    if (objectClass) {
        g_methodTransformGetName =
            p_il2cpp_class_get_method_from_name(objectClass, "get_name", 0);
        Log("[AM-IL2CPP] Object.get_name: %p", g_methodTransformGetName);
    }

    // Transform.get_parent
    void* trClass = FindClassInAssemblies(asms, asmCount, "UnityEngine", "Transform");
    if (trClass) {
        g_methodTransformGetParent =
            p_il2cpp_class_get_method_from_name(trClass, "get_parent", 0);
        Log("[AM-IL2CPP] Transform.get_parent: %p", g_methodTransformGetParent);
    }

    // TMP_Text class
    g_classTMP_Text = FindClassInAssemblies(asms, asmCount, "TMPro", "TMP_Text");
    if (g_classTMP_Text) {
        g_methodGetText = p_il2cpp_class_get_method_from_name(g_classTMP_Text, "get_text", 0);
        g_methodSetText = p_il2cpp_class_get_method_from_name(g_classTMP_Text, "set_text", 1);
        Log("[AM-IL2CPP] TMP_Text: class=%p get_text=%p set_text=%p",
            g_classTMP_Text, g_methodGetText, g_methodSetText);
    } else {
        Log("[AM-IL2CPP] WARNING: TMPro.TMP_Text not found");
    }

    g_il2cppResolved = true;
    Log("[AM-IL2CPP] Scanner initialized successfully");
    return true;
}

// ============================================================================
// UID Control — SetActive-based
// ============================================================================

static void* TryFindGO(const char* path) {
    if (!g_methodGameObjectFind || !p_il2cpp_string_new) return nullptr;
    void* dom = p_il2cpp_domain_get();
    p_il2cpp_thread_attach(dom);
    void* s = p_il2cpp_string_new(path);
    void* p[1] = { s };
    return SafeInvoke(g_methodGameObjectFind, nullptr, p);
}

static bool FindUidComponent() {
    if (!g_il2cppResolved || !g_methodGameObjectFind || !p_il2cpp_string_new) {
        Log("[AM-UID] IL2CPP not ready for UID lookup");
        return false;
    }

    void* dom = p_il2cpp_domain_get();
    p_il2cpp_thread_attach(dom);

    // Find UIDPanelPanel
    {
        void* s = p_il2cpp_string_new("UINode/UIRoot/UIDPanelPanel");
        void* p[1] = { s };
        g_uidPanelGO = SafeInvoke(g_methodGameObjectFind, nullptr, p);
    }
    if (!g_uidPanelGO) {
        Log("[AM-UID] UIDPanelPanel not found");
        return false;
    }
    Log("[AM-UID] Found UIDPanelPanel: %p", g_uidPanelGO);

    // Find UID Text GO
    {
        void* s = p_il2cpp_string_new("UINode/UIRoot/UIDPanelPanel/Main/TextNode/Text");
        void* p[1] = { s };
        g_uidTextGO = SafeInvoke(g_methodGameObjectFind, nullptr, p);
    }
    Log("[AM-UID] UID Text GO: %p", g_uidTextGO);

    // Find Ping GO
    {
        void* s = p_il2cpp_string_new("UINode/UIRoot/UIDPanelPanel/Main/TextNode/PingCon");
        void* p[1] = { s };
        g_uidPingGO = SafeInvoke(g_methodGameObjectFind, nullptr, p);
    }
    Log("[AM-UID] Ping GO: %p", g_uidPingGO);

    return (g_uidTextGO != nullptr || g_uidPingGO != nullptr);
}

static void GameObjectSetActive(void* go, bool active) {
    if (!go || !g_methodGameObjectSetActive) return;
    void* dom = p_il2cpp_domain_get();
    p_il2cpp_thread_attach(dom);
    __try {
        int32_t val = active ? 1 : 0;
        void* params[1] = { &val };
        SafeInvoke(g_methodGameObjectSetActive, go, params);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("[AM-UID] Exception in SetActive");
    }
}

static void SetUidVisible(bool visible) {
    g_uidHidden = !visible;
    if (!g_uidTextGO) {
        g_uidTextGO = TryFindGO("UINode/UIRoot/UIDPanelPanel/Main/TextNode/Text");
    }
    if (!g_uidTextGO) return;
    GameObjectSetActive(g_uidTextGO, visible);
}

static void SetPingVisible(bool visible) {
    g_pingHidden = !visible;
    if (!g_uidPingGO) {
        g_uidPingGO = TryFindGO("UINode/UIRoot/UIDPanelPanel/Main/TextNode/PingCon");
    }
    if (!g_uidPingGO) return;
    GameObjectSetActive(g_uidPingGO, visible);
}

// ============================================================================
// set_text Hook — cached pointer architecture
// ============================================================================

#include "MinHook.h"

struct MInfo { void* mp; };

typedef void* (*tNativeGetGO)(void* __this, void* methodInfo);
typedef void* (*tNativeGetName)(void* __this, void* methodInfo);
typedef void* (*tNativeGetTransform)(void* __this, void* methodInfo);
typedef void* (*tNativeGetParent)(void* __this, void* methodInfo);
typedef void (*tTMPSetText)(void* __this, void* value, void* methodInfo);

static tTMPSetText g_origSetText = nullptr;
static bool g_setTextHooked = false;
static tNativeGetGO        g_nativeGetGO = nullptr;
static tNativeGetName      g_nativeGetName = nullptr;
static tNativeGetTransform g_nativeGetTransform = nullptr;
static tNativeGetParent    g_nativeGetParent = nullptr;
static void*          g_cachedEmptyStr = nullptr;
static volatile bool  g_hookDisabled = false;

// Component pointer cache — avoids native calls for known components
enum HideType { HT_UID = 0, HT_MENU_NAME, HT_CARD_UID, HT_CARD_NAME };
struct CachedComp { void* comp; HideType type; };
static CachedComp g_compCache[16] = {};
static volatile int g_compCacheCount = 0;

static bool IsHideActive(HideType t) {
    switch (t) {
        case HT_UID:       return g_uidHidden || g_menuUidHidden;
        case HT_MENU_NAME: return g_menuNameHidden;
        case HT_CARD_UID:  return g_cardUidHidden;
        case HT_CARD_NAME: return g_cardNameHidden;
        default: return false;
    }
}

static void CacheComp(void* comp, HideType type) {
    int n = g_compCacheCount;
    for (int i = 0; i < n; i++) {
        if (g_compCache[i].comp == comp) return;
    }
    if (n < 16) {
        g_compCache[n].comp = comp;
        g_compCache[n].type = type;
        g_compCacheCount = n + 1;
    }
}

static void hkTMPSetText(void* __this, void* value, void* methodInfo) {
    // FAST PATH: cached pointer check (no native calls — deadlock-proof)
    int n = g_compCacheCount;
    for (int i = 0; i < n; i++) {
        if (g_compCache[i].comp == __this && IsHideActive(g_compCache[i].type)) {
            g_origSetText(__this, g_cachedEmptyStr, methodInfo);
            return;
        }
    }

    // SLOW PATH: identify new components by GO name (only when game is alive)
    if (!g_hookDisabled && g_nativeGetGO && g_nativeGetName) {
        bool anyHidden = g_uidHidden || g_menuUidHidden || g_menuNameHidden ||
                         g_cardUidHidden || g_cardNameHidden;
        if (anyHidden) {
            __try {
                void* go = g_nativeGetGO(__this, g_methodComponentGetGameObject);
                if (go) {
                    void* nameStr = g_nativeGetName(go, g_methodTransformGetName);
                    if (nameStr) {
                        char name[64];
                        if (ReadIL2CppString(nameStr, name, sizeof(name)) > 0) {
                            if (strcmp(name, "ManagerNumber") == 0) {
                                CacheComp(__this, HT_UID);
                                if (g_uidHidden || g_menuUidHidden) {
                                    g_origSetText(__this, g_cachedEmptyStr, methodInfo);
                                    return;
                                }
                            }
                            else if (strcmp(name, "ManagerName") == 0) {
                                CacheComp(__this, HT_MENU_NAME);
                                if (g_menuNameHidden) {
                                    g_origSetText(__this, g_cachedEmptyStr, methodInfo);
                                    return;
                                }
                            }
                            else if (strcmp(name, "PlayerUidTxt") == 0) {
                                CacheComp(__this, HT_CARD_UID);
                                if (g_cardUidHidden) {
                                    g_origSetText(__this, g_cachedEmptyStr, methodInfo);
                                    return;
                                }
                            }
                            else if (strcmp(name, "NameTxt") == 0) {
                                // Walk up ancestors looking for PersonalInfoNode (business card only)
                                bool isCard = false;
                                if (g_nativeGetTransform && g_nativeGetParent) {
                                    void* cur = g_nativeGetTransform(__this, g_methodComponentGetTransform);
                                    for (int lvl = 0; cur && lvl < 6; lvl++) {
                                        cur = g_nativeGetParent(cur, g_methodTransformGetParent);
                                        if (!cur) break;
                                        void* pName = g_nativeGetName(cur, g_methodTransformGetName);
                                        if (pName) {
                                            char pn[32];
                                            if (ReadIL2CppString(pName, pn, sizeof(pn)) > 0 &&
                                                strcmp(pn, "PersonalInfoNode") == 0) {
                                                isCard = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (isCard) {
                                    CacheComp(__this, HT_CARD_NAME);
                                    if (g_cardNameHidden) {
                                        g_origSetText(__this, g_cachedEmptyStr, methodInfo);
                                        return;
                                    }
                                }
                            }
                        }
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    g_origSetText(__this, value, methodInfo);
}

static bool InstallSetTextHook() {
    if (!g_methodSetText || !g_methodComponentGetGameObject || !g_methodTransformGetName)
        return false;

    g_nativeGetGO = (tNativeGetGO)((MInfo*)g_methodComponentGetGameObject)->mp;
    g_nativeGetName = (tNativeGetName)((MInfo*)g_methodTransformGetName)->mp;
    if (!g_nativeGetGO || !g_nativeGetName) return false;

    if (g_methodComponentGetTransform)
        g_nativeGetTransform = (tNativeGetTransform)((MInfo*)g_methodComponentGetTransform)->mp;
    if (g_methodTransformGetParent)
        g_nativeGetParent = (tNativeGetParent)((MInfo*)g_methodTransformGetParent)->mp;

    g_cachedEmptyStr = p_il2cpp_string_new("");

    void* mp = ((MInfo*)g_methodSetText)->mp;
    if (!mp) return false;

    MH_STATUS initStatus = MH_Initialize();
    if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        Log("[AM-UID] MH_Initialize failed: %d", initStatus);
        return false;
    }
    if (MH_CreateHook(mp, (void*)hkTMPSetText, (void**)&g_origSetText) != MH_OK) {
        Log("[AM-UID] MH_CreateHook for set_text failed");
        return false;
    }
    MH_EnableHook(mp);
    g_setTextHooked = true;
    Log("[AM-UID] Hooked TMP_Text.set_text: %p (getGO=%p, getName=%p)",
        mp, g_nativeGetGO, g_nativeGetName);
    return true;
}

// ============================================================================
// Toggle functions (hook handles blocking by cached pointer + GO name)
// ============================================================================

static void SetMenuUidVisible(bool visible)  { g_menuUidHidden = !visible; }
static void SetMenuNameVisible(bool visible) { g_menuNameHidden = !visible; }
static void SetCardUidVisible(bool visible)  { g_cardUidHidden = !visible; }
static void SetCardNameVisible(bool visible) { g_cardNameHidden = !visible; }
