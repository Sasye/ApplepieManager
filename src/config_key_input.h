#pragma once
#include <windows.h>
#include <atomic>
#include <string>

// Window-message capture, owned by the panel thread. Only suppress_home() is
// called by the existing HOME watcher; no extra polling or keyboard hook.
class ConfigKeyCapture {
    enum class State { Idle, Waiting, Captured, Cancelled };
    State state_ = State::Idle;
    int key_ = 0;
    std::atomic<unsigned> home_gate_{0}; // 0 idle, 1 capture, 2 wait for HOME release
public:
    void begin() { key_ = 0; state_ = State::Waiting; home_gate_.store(1); }
    bool open() const { return state_ != State::Idle; }
    bool captured() const { return state_ == State::Captured; }
    bool cancelled() const { return state_ == State::Cancelled; }
    int key() const { return key_; }
    void cancel() { if (open()) state_ = State::Cancelled; }
    void finish() { state_ = State::Idle; home_gate_.store(2); }
    bool suppress_home(bool down) {
        if (!down) { unsigned ended = 2; home_gate_.compare_exchange_strong(ended, 0); }
        return home_gate_.load() != 0;
    }
    bool message(UINT msg, WPARAM vk, LPARAM flags) {
        if (!open()) return false;
        if (msg == WM_KILLFOCUS || (msg == WM_ACTIVATEAPP && !vk) || (msg == WM_SHOWWINDOW && !vk)) {
            cancel(); return false;
        }
        if (msg == WM_CHAR || msg == WM_SYSCHAR) return true;
        if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) return false;
        // Ignore repeats and the held Enter/Space that opened the button.
        if (state_ == State::Waiting && !(flags & (LPARAM(1) << 30)) && vk > 0 && vk < 256) {
            if (vk == VK_ESCAPE) cancel();
            else { key_ = int(vk); state_ = State::Captured; }
        }
        return true;
    }
};

inline std::string ConfigKeyName(int vk, bool chinese) {
    if (!vk) return chinese ? u8"未绑定" : "Unbound";
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::string(1, char(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return "Numpad " + std::to_string(vk - VK_NUMPAD0);
    switch (vk) {
    case VK_BACK: return "Backspace"; case VK_TAB: return "Tab"; case VK_RETURN: return "Enter";
    case VK_ESCAPE: return "Esc"; case VK_SPACE: return "Space"; case VK_CAPITAL: return "Caps Lock";
    case VK_SHIFT: return "Shift"; case VK_CONTROL: return "Ctrl"; case VK_MENU: return "Alt";
    case VK_LSHIFT: return "Left Shift"; case VK_RSHIFT: return "Right Shift";
    case VK_LCONTROL: return "Left Ctrl"; case VK_RCONTROL: return "Right Ctrl";
    case VK_LMENU: return "Left Alt"; case VK_RMENU: return "Right Alt";
    case VK_INSERT: return "Insert"; case VK_DELETE: return "Delete";
    case VK_HOME: return "Home"; case VK_END: return "End";
    case VK_PRIOR: return "Page Up"; case VK_NEXT: return "Page Down";
    case VK_LEFT: return "Left"; case VK_RIGHT: return "Right";
    case VK_UP: return "Up"; case VK_DOWN: return "Down";
    case VK_LWIN: return "Left Win"; case VK_RWIN: return "Right Win"; case VK_APPS: return "Menu";
    case VK_SNAPSHOT: return "Print Screen"; case VK_PAUSE: return "Pause";
    case VK_NUMLOCK: return "Num Lock"; case VK_SCROLL: return "Scroll Lock";
    case VK_MULTIPLY: return "Numpad *"; case VK_ADD: return "Numpad +";
    case VK_SUBTRACT: return "Numpad -"; case VK_DECIMAL: return "Numpad ."; case VK_DIVIDE: return "Numpad /";
    case VK_LBUTTON: return "Mouse Left"; case VK_RBUTTON: return "Mouse Right";
    case VK_MBUTTON: return "Mouse Middle"; case VK_XBUTTON1: return "Mouse X1"; case VK_XBUTTON2: return "Mouse X2";
    }
    wchar_t name[64]{}; char utf8[256]{};
    const UINT scan = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
    if (scan && GetKeyNameTextW(LONG(scan << 16), name, 64) &&
        WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof(utf8), nullptr, nullptr)) return utf8;
    return chinese ? u8"未知按键" : "Unknown key";
}
