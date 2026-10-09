// Real Core, separate processes, fixture files beside this executable only.
#define NOMINMAX
#include "../applepie_client.h"
#include "../../src/hub_core.h"
#include "../../src/config_key_input.h"
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <thread>
#include <atomic>
static std::atomic<unsigned> checks{0};
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); fflush(stdout); ExitProcess(10); } } while (0)
static APH_ConfigApi api;
static std::wstring directory;
static APH_ConfigOption modes[]{{"light"}, {"dark"}, {"system"}};
static APH_Result APH_CALL Restore(APH_Owner, const APH_Cleanup*, void*) { return APH_OK; }
static APH_Owner Add(const char* id, bool notices = false) {
    APH_Manifest m{sizeof(m), 1, 1, APH_ARCH_X64, APH_PLUGIN_LIFECYCLE, APH_CAP_CORE, 0};
    if (notices) m.flags |= APH_PLUGIN_MODULE_OBSERVER;
    strcpy_s(m.id, id); strcpy_s(m.display_name, id);
    const auto owner = hub::add_owner(m, GetModuleHandleW(nullptr)); CHECK(owner);
    const APH_AttachArgs args{sizeof(args), 0, owner, hub::api()};
    applepie::Client client; CHECK(client.Attach(&args) == APH_OK); CHECK(client.Service(api) == APH_OK);
    const APH_LifecycleDesc life{sizeof(life), 2000, nullptr, Restore, nullptr}; CHECK(client.RegisterLifecycle(life) == APH_OK);
    CHECK(hub::begin_enable(owner)); CHECK(hub::finish_enable(owner, APH_OK) == APH_OK);
    return owner;
}
static std::wstring Path(const char* id) {
    const std::string s(id); return directory + L"\\config\\applepie." + std::wstring(s.begin(), s.end()) + L".ini";
}
static void Write(const std::wstring& path, const std::string& value) {
    CreateDirectoryW((directory + L"\\config").c_str(), nullptr);
    std::ofstream out(path, std::ios::binary | std::ios::trunc); CHECK(out.good()); out << value; out.close(); CHECK(!out.fail());
}
static std::string Read(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary); CHECK(in.good());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
static std::vector<APH_ConfigField> Fields() {
    std::vector<APH_ConfigField> result(5);
    const char* keys[]{"enabled", "count", "gain", "mode", "caption"};
    for (unsigned i = 0; i < result.size(); ++i) {
        auto& f = result[i]; f.size = sizeof(f); f.type = f.initial.type = i + 1;
        strcpy_s(f.key, keys[i]); strcpy_s(f.label, keys[i]);
    }
    result[0].initial.integer = 1;
    result[1].minimum = 0; result[1].maximum = 1000; result[1].initial.integer = 10;
    result[2].minimum = 0.25; result[2].maximum = 2.0; result[2].initial.number = 1.0;
    result[3].option_count = 3; result[3].options = modes; strcpy_s(result[3].initial.text, "system");
    result[4].text_max_bytes = 120; strcpy_s(result[4].initial.text, "default");
    return result;
}
static APH_Result Register(APH_Owner owner, const std::vector<APH_ConfigField>& fields) {
    const APH_ConfigSchema s{sizeof(s), (uint32_t)fields.size(), fields.data()}; return api.register_schema(owner, &s);
}
static APH_ConfigSnapshot Snapshot(APH_Owner owner, std::vector<APH_ConfigValue>* values = nullptr) {
    APH_ConfigSnapshot s{}; CHECK(api.snapshot(owner, sizeof(s), &s, 0, nullptr) == APH_OK);
    if (values) { values->resize(s.field_count); CHECK(api.snapshot(owner, sizeof(s), &s, s.field_count, values->data()) == APH_OK); }
    return s;
}
static APH_ConfigSnapshot Done(APH_Owner owner, APH_Result expected = APH_OK) {
    const auto deadline = GetTickCount64() + 5000;
    for (;;) { const auto s = Snapshot(owner); if (!s.pending) { CHECK(s.last_result == expected); return s; }
        CHECK(GetTickCount64() < deadline); Sleep(1); }
}
static void Save(APH_Owner owner, uint64_t revision, const std::vector<APH_ConfigValue>& v) {
    CHECK(api.save(owner, revision, (uint32_t)v.size(), v.data()) == APH_PENDING);
}
static void Behavior() {
    const auto path = Path("verify.config.base");
    Write(path, "# comment\nenabled=true\ncount=invalid\ngain=0.75\nmode=dark\ncaption=  original  \nfuture_setting=keep\n");
    auto fields = Fields(); const auto owner = Add("verify.config.base");
    CHECK(Register(owner, fields) == APH_OK); CHECK(Register(owner, fields) == APH_E_DUPLICATE);
    std::vector<APH_ConfigValue> values; auto s = Snapshot(owner, &values);
    CHECK(s.revision == 1 && s.field_count == 5 && s.defaulted_fields == 1);
    CHECK(values[1].integer == 10 && values[2].number == .75 && !strcmp(values[3].text, "dark") && !strcmp(values[4].text, "  original  "));
    strcpy_s(fields[0].key, "mutated"); strcpy_s(modes[0].value, "mutated");
    APH_ConfigField f{}; APH_ConfigOption option{};
    CHECK(api.field(owner, 0, sizeof(f), &f) == APH_OK && !strcmp(f.key, "enabled"));
    CHECK(api.field(owner, 3, sizeof(f), &f) == APH_OK && !f.options && f.option_count == 3);
    CHECK(api.option(owner, 3, 0, sizeof(option), &option) == APH_OK && !strcmp(option.value, "light"));
    strcpy_s(modes[0].value, "light");
    CHECK(api.snapshot(owner, sizeof(s) - 1, &s, 0, nullptr) == APH_E_ABI);
    CHECK(api.snapshot(owner, sizeof(s), (APH_ConfigSnapshot*)1, 0, nullptr) == APH_E_ARGUMENT);
    CHECK(api.snapshot(owner, sizeof(s), &s, 4, values.data()) == APH_E_ARGUMENT);
    CHECK(api.snapshot(owner, sizeof(s), &s, 5, (APH_ConfigValue*)1) == APH_E_ARGUMENT);
    CHECK(api.save(owner, 1, 5, (const APH_ConfigValue*)1) == APH_E_ARGUMENT);
    auto bad = values; bad[0].integer = 2; CHECK(api.save(owner, 1, 5, bad.data()) == APH_E_ARGUMENT);
    bad = values; bad[1].integer = 1001; CHECK(api.save(owner, 1, 5, bad.data()) == APH_E_ARGUMENT);
    bad = values; bad[2].number = std::numeric_limits<double>::quiet_NaN(); CHECK(api.save(owner, 1, 5, bad.data()) == APH_E_ARGUMENT);
    bad[2].number = std::numeric_limits<double>::infinity(); CHECK(api.save(owner, 1, 5, bad.data()) == APH_E_ARGUMENT);
    bad = values; strcpy_s(bad[3].text, "missing"); CHECK(api.save(owner, 1, 5, bad.data()) == APH_E_ARGUMENT);
    bad = values; strcpy_s(bad[4].text, "line\nbreak"); CHECK(api.save(owner, 1, 5, bad.data()) == APH_E_ARGUMENT);
    bad[4].text[0] = -1; bad[4].text[1] = 0; CHECK(api.save(owner, 1, 5, bad.data()) == APH_E_ARGUMENT);
    memset(bad[4].text, 'x', sizeof(bad[4].text)); CHECK(api.save(owner, 1, 5, bad.data()) == APH_E_ARGUMENT);
    CHECK(Snapshot(owner).revision == 1);
    values[0].integer = 0; values[1].integer = 42; values[2].number = 1.25;
    strcpy_s(values[4].text, u8"  中文=#内容  "); Save(owner, 1, values); CHECK(Done(owner).revision == 2);
    const auto saved = Read(path); CHECK(saved.find("future_setting=keep\n") != std::string::npos);
    CHECK(saved.find(u8"caption=  中文=#内容  \n") != std::string::npos);
    CHECK(api.save(owner, 1, 5, values.data()) == APH_E_STALE);

    // OS sharing denies atomic replacement: disk, values and revision stay intact.
    HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr); CHECK(held != INVALID_HANDLE_VALUE);
    values[1].integer = 99; Save(owner, 2, values); CHECK(Done(owner, APH_E_IO).revision == 2);
    CHECK(Snapshot(owner, &values).win32_error != 0 && values[1].integer == 42); CloseHandle(held);
    CHECK(Read(path) == saved);

    // Two same-revision submissions: one wins, one must be Busy/Stale.
    auto a = values, b = values; a[1].integer = 111; b[1].integer = 222;
    std::atomic<bool> go{false}; APH_Result ra = 0, rb = 0;
    std::thread ta([&] { while (!go.load()) SwitchToThread(); ra = api.save(owner, 2, 5, a.data()); });
    std::thread tb([&] { while (!go.load()) SwitchToThread(); rb = api.save(owner, 2, 5, b.data()); });
    go = true; ta.join(); tb.join();
    CHECK((ra == APH_PENDING && (rb == APH_E_BUSY || rb == APH_E_STALE)) || (rb == APH_PENDING && (ra == APH_E_BUSY || ra == APH_E_STALE)));
    CHECK(Done(owner).revision == 3); Snapshot(owner, &values); CHECK(values[1].integer == (ra == APH_PENDING ? 111 : 222));

    APH_LifecycleApi life{}; CHECK(hub::api()->copy_service(owner, APH_SERVICE_LIFECYCLE, 1, sizeof(life), &life) == APH_OK);
    CHECK(life.disable(owner) == APH_PENDING);
    const auto deadline = GetTickCount64() + 5000;
    for (;;) { APH_OwnerStatus status{}; hub::api()->owner_status(owner, sizeof(status), &status);
        if (status.state == APH_OWNER_DISABLED) break; CHECK(GetTickCount64() < deadline); Sleep(1); }
    values[1].integer = 42; Save(owner, 3, values); CHECK(Done(owner).revision == 4);
    CHECK(hub::begin_enable(owner)); CHECK(hub::finish_enable(owner, APH_OK) == APH_OK);
    Snapshot(owner, &values); CHECK(values[1].integer == 42);

    // A business schema must not open or overwrite Manager's preferences.
    const auto reserved = Add("manager"); Write(Path("manager"), "language=zh\n");
    fields = Fields(); CHECK(Register(reserved, fields) == APH_E_ARGUMENT);
    CHECK(Read(Path("manager")) == "language=zh\n"); CHECK(DeleteFileW(Path("manager").c_str()));

    // Invalid schema attempts don't publish a partial schema.
    const auto invalid = Add("verify.config.invalid"); DeleteFileW(Path("verify.config.invalid").c_str());
    fields = Fields(); fields[0].initial.integer = 2; CHECK(Register(invalid, fields) == APH_E_ARGUMENT);
    fields = Fields(); strcpy_s(fields[1].key, "enabled"); CHECK(Register(invalid, fields) == APH_E_DUPLICATE);
    fields = Fields(); fields[3].options = (const APH_ConfigOption*)1; CHECK(Register(invalid, fields) == APH_E_ARGUMENT);
    fields = Fields(); fields[1].minimum = .5; CHECK(Register(invalid, fields) == APH_E_ARGUMENT);
    fields = Fields(); CHECK(Register(invalid, fields) == APH_OK); CHECK(Snapshot(invalid).field_count == 5);

    // Field and option storage grows with the registered schema.
    const auto many = Add("verify.config.many"); DeleteFileW(Path("verify.config.many").c_str());
    std::vector<APH_ConfigField> large(320);
    std::vector<APH_ConfigOption> options(20);
    for (unsigned i = 0; i < options.size(); ++i) sprintf_s(options[i].value, "mode%u", i);
    for (unsigned i = 0; i < large.size(); ++i) {
        auto& x = large[i]; x.size = sizeof(x); x.type = x.initial.type = APH_CONFIG_TEXT; x.text_max_bytes = 255;
        sprintf_s(x.key, "field%u", i); strcpy_s(x.label, x.key); memset(x.initial.text, 'v', 255);
    }
    large[0].type = large[0].initial.type = APH_CONFIG_ENUM; large[0].option_count = (uint32_t)options.size(); large[0].options = options.data(); strcpy_s(large[0].initial.text, "mode19");
    CHECK(Register(many, large) == APH_OK); CHECK(Snapshot(many, &values).field_count == 320);
    CHECK(api.option(many, 0, 19, sizeof(option), &option) == APH_OK && !strcmp(option.value, "mode19"));
    Save(many, 1, values); CHECK(Done(many).revision == 2); CHECK(Read(Path("verify.config.many")).size() > 65536);
    puts("PASS config behavior: validation, copied schema, UTF-8, atomic failure, stale edits, concurrency, lifecycle and dynamic counts");
}
static void Persistence() {
    const auto owner = Add("verify.config.base"); auto fields = Fields(); std::swap(modes[0], modes[1]);
    CHECK(Register(owner, fields) == APH_OK);
    std::vector<APH_ConfigValue> values; const auto s = Snapshot(owner, &values);
    CHECK(s.revision == 1 && !s.defaulted_fields && !s.pending && values[1].integer == 42 && values[2].number == 1.25);
    CHECK(!strcmp(values[3].text, "dark") && !strcmp(values[4].text, u8"  中文=#内容  "));
    puts("PASS config persistence: fresh process, typed values and stable enum strings");
}
static void Corrupt() {
    const auto path = Path("verify.config.corrupt"); const auto owner = Add("verify.config.corrupt"); auto fields = Fields();
    const std::string damaged = "count=12\ncount=13\n"; Write(path, damaged);
    CHECK(Register(owner, fields) == APH_E_IO); CHECK(Read(path) == damaged);
    APH_ConfigSnapshot s{}; CHECK(api.snapshot(owner, sizeof(s), &s, 0, nullptr) == APH_E_UNAVAILABLE);
    Write(path, "caption=\xFF\n"); CHECK(Register(owner, fields) == APH_E_IO);
    Write(path, "gain=nan\ncount=2147483648\n"); CHECK(Register(owner, fields) == APH_OK);
    CHECK(Snapshot(owner).defaulted_fields == 2);
    puts("PASS config corruption: unreadable files remain untouched, invalid scalar values use defaults");
}
static void Plugin() {
    const auto fixture = directory + L"\\config-fixture"; CreateDirectoryW(fixture.c_str(), nullptr);
    const auto dll = fixture + L"\\applepie_config_example.dll";
    CHECK(CopyFileW((directory + L"\\applepie_config_example.dll").c_str(), dll.c_str(), FALSE));
    const auto path = fixture + L"\\config\\applepie.example.config.ini"; DeleteFileW(path.c_str());
    const auto module = LoadLibraryW(dll.c_str()); CHECK(module);
    const auto manifest = (const APH_Manifest*)GetProcAddress(module, "APH_PluginManifest"); CHECK(manifest);
    const auto owner = hub::add_owner(*manifest, module); CHECK(owner);
    const auto attach = (APH_AttachFn)GetProcAddress(module, "APH_Attach"); const auto enable = (APH_EnableFn)GetProcAddress(module, "APH_Enable");
    const auto read = (int(*)())GetProcAddress(module, "APH_ConfigExampleRefreshMs"); CHECK(attach && enable && read);
    CHECK(hub::attach(owner, attach) == APH_OK);
    CHECK(hub::api()->copy_service(owner, APH_SERVICE_CONFIG, 1, sizeof(api), &api) == APH_OK);
    auto epoch = hub::begin_enable(owner); CHECK(hub::enable(owner, enable, epoch) == APH_OK); CHECK(hub::finish_enable(owner, APH_OK, epoch) == APH_OK);
    CHECK(read() == 100);
    std::vector<APH_ConfigValue> values; auto s = Snapshot(owner, &values); values[1].integer = 250;
    Save(owner, s.revision, values); Done(owner);
    const auto notifyDeadline = GetTickCount64() + 5000;
    while (read() != 250) { CHECK(GetTickCount64() < notifyDeadline); Sleep(1); }
    APH_LifecycleApi life{}; CHECK(hub::api()->copy_service(owner, APH_SERVICE_LIFECYCLE, 1, sizeof(life), &life) == APH_OK);
    CHECK(life.disable(owner) == APH_PENDING);
    const auto deadline = GetTickCount64() + 5000;
    for (;;) { APH_OwnerStatus state{}; hub::api()->owner_status(owner, sizeof(state), &state);
        if (state.state == APH_OWNER_DISABLED) break; CHECK(GetTickCount64() < deadline); Sleep(1); }
    CHECK(read() == -1); epoch = hub::begin_enable(owner); CHECK(epoch);
    CHECK(hub::enable(owner, enable, epoch) == APH_OK); CHECK(hub::finish_enable(owner, APH_OK, epoch) == APH_OK); CHECK(read() == 250);
    puts("PASS config example: real DLL, settings application and disable/re-enable");
}
struct NoticeState {
    HANDLE entered = nullptr, release = nullptr;
    std::atomic<unsigned> calls{0}, completed{0}, active{0}, overlap{0};
    std::atomic<int> value{0}, result{APH_OK};
    std::atomic<bool> fault{false};
    DWORD caller = 0;
};
static NoticeState notices;
static void APH_CALL Changed(APH_Owner owner, void* context) {
    CHECK(context == &notices && GetCurrentThreadId() != notices.caller);
    const auto call = ++notices.calls;
    if (++notices.active != 1) ++notices.overlap;
    __try {
        if (notices.fault) { volatile int value = *reinterpret_cast<volatile int*>(uintptr_t(1)); (void)value; }
        APH_ConfigSnapshot s{}; APH_ConfigValue values[5]{};
        // Reentry proves that callbacks do not run under the configuration lock.
        CHECK(api.snapshot(owner, sizeof(s), &s, 5, values) == APH_OK);
        notices.value = values[1].integer; notices.result = s.last_result;
        if (call == 1) {
            SetEvent(notices.entered);
            CHECK(WaitForSingleObject(notices.release, 5000) == WAIT_OBJECT_0);
        }
    } __finally { --notices.active; ++notices.completed; }
}
static void WaitNotices(unsigned count) {
    const auto deadline = GetTickCount64() + 5000;
    while (notices.completed < count) { CHECK(GetTickCount64() < deadline); Sleep(1); }
}
static void Notifications() {
    const auto denied = Add("verify.config.no-observer"); auto fields = Fields();
    CHECK(Register(denied, fields) == APH_OK);
    CHECK(api.set_notifier(denied, Changed, &notices) == APH_E_CAPABILITY);
    const auto owner = Add("verify.config.notices", true);
    const auto path = Path("verify.config.notices"); DeleteFileW(path.c_str());
    CHECK(api.set_notifier(owner, Changed, &notices) == APH_E_UNAVAILABLE);
    CHECK(Register(owner, fields) == APH_OK);
    CHECK(api.set_notifier(owner, (APH_ConfigNotifyFn)1, nullptr) == APH_E_ARGUMENT);
    APH_ConfigApi prefix{};
    CHECK(hub::api()->copy_service(owner, APH_SERVICE_CONFIG, 1, APH_CONFIG_API_V1_PREFIX_BYTES, &prefix) == APH_OK);
    CHECK(prefix.size == APH_CONFIG_API_V1_PREFIX_BYTES && prefix.save && !prefix.set_notifier);
    CHECK(hub::api()->copy_service(owner, APH_SERVICE_CONFIG, 1, APH_CONFIG_API_V1_PREFIX_BYTES - 1, &prefix) == APH_E_ABI);
    notices.entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    notices.release = CreateEventW(nullptr, TRUE, FALSE, nullptr); CHECK(notices.entered && notices.release);
    notices.caller = GetCurrentThreadId();
    CHECK(api.set_notifier(owner, Changed, &notices) == APH_OK);
    CHECK(api.set_notifier(owner, Changed, &notices) == APH_E_DUPLICATE);
    CHECK(!notices.calls); // initial snapshot is an explicit read, not a periodic notice
    std::vector<APH_ConfigValue> values; auto s = Snapshot(owner, &values); values[1].integer = 101;
    Save(owner, s.revision, values);
    CHECK(WaitForSingleObject(notices.entered, 5000) == WAIT_OBJECT_0);
    CHECK(notices.value == 101 && Snapshot(owner).revision == 2);
    values[1].integer = 102; Save(owner, 2, values); CHECK(Done(owner).revision == 3);
    CHECK(notices.calls == 1 && notices.active == 1 && !notices.overlap);
    SetEvent(notices.release); WaitNotices(2);
    CHECK(notices.value == 102 && !notices.active && !notices.overlap);
    const auto stored = Read(path);
    HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(held != INVALID_HANDLE_VALUE); values[1].integer = 103; Save(owner, 3, values); WaitNotices(3);
    CHECK(notices.result == APH_E_IO && notices.value == 102 && Snapshot(owner).revision == 3);
    CloseHandle(held); CHECK(Read(path) == stored);
    values[1].integer = 9999; CHECK(api.save(owner, 3, 5, values.data()) == APH_E_ARGUMENT);
    CHECK(notices.calls == 3);
    APH_LifecycleApi life{}; CHECK(hub::api()->copy_service(owner, APH_SERVICE_LIFECYCLE, 1, sizeof(life), &life) == APH_OK);
    CHECK(life.disable(owner) == APH_PENDING);
    const auto deadline = GetTickCount64() + 5000;
    for (;;) { APH_OwnerStatus status{}; CHECK(hub::api()->owner_status(owner, sizeof(status), &status) == APH_OK);
        if (status.state == APH_OWNER_DISABLED) break; CHECK(GetTickCount64() < deadline); Sleep(1); }
    values[1].integer = 104; Save(owner, 3, values); WaitNotices(4);
    CHECK(notices.value == 104 && notices.result == APH_OK);
    CHECK(hub::begin_enable(owner)); CHECK(hub::finish_enable(owner, APH_OK) == APH_OK);
    Sleep(50); CHECK(notices.calls == 4); // no clock-driven repeat on idle/re-enable
    notices.fault = true; values[1].integer = 105; Save(owner, 4, values); WaitNotices(5);
    APH_OwnerStatus status{}; CHECK(hub::api()->owner_status(owner, sizeof(status), &status) == APH_OK);
    CHECK(status.state == APH_OWNER_NEEDS_RESTART && !notices.active);
    values[1].integer = 106; Save(owner, 5, values); CHECK(Done(owner).revision == 6);
    Sleep(50); CHECK(notices.calls == 5); // quarantined module is never notified again
    CHECK(api.set_notifier(owner, Changed, &notices) == APH_E_REVOKED);
    CloseHandle(notices.entered); CloseHandle(notices.release);
    puts("PASS config notifications: old ABI prefix, callback validation, serial/reentrant delivery, failure/disabled saves, SEH quarantine and no idle polling");
}
static void KeyCaptureChecks() {
    ConfigKeyCapture capture;
    CHECK(!capture.open() && !capture.suppress_home(false));
    CHECK(!capture.message(WM_KEYDOWN, 'Q', 0));
    capture.begin(); CHECK(capture.open() && capture.suppress_home(false));
    CHECK(capture.message(WM_KEYDOWN, VK_RETURN, LPARAM(1) << 30) && !capture.captured());
    CHECK(!capture.message(WM_KEYUP, VK_RETURN, 0) && !capture.captured());
    CHECK(capture.message(WM_CHAR, 'q', 0) && !capture.captured());
    CHECK(capture.message(WM_KEYDOWN, VK_F11, 0) && capture.captured() && capture.key() == VK_F11);
    CHECK(capture.message(WM_KEYDOWN, 'Q', 0) && capture.key() == VK_F11); // first key wins
    capture.finish(); CHECK(!capture.open() && !capture.suppress_home(false));
    capture.begin(); CHECK(capture.message(WM_SYSKEYDOWN, VK_MENU, 0) && capture.key() == VK_MENU);
    capture.message(WM_KILLFOCUS, 0, 0); CHECK(capture.cancelled() && !capture.captured());
    capture.finish(); capture.begin();
    CHECK(capture.message(WM_KEYDOWN, VK_ESCAPE, 0) && capture.cancelled() && !capture.captured());
    capture.finish(); capture.begin();
    CHECK(capture.message(WM_KEYDOWN, 'Q', 0) && capture.captured() && capture.key() == 'Q');
    capture.message(WM_SHOWWINDOW, FALSE, 0); CHECK(capture.cancelled());
    capture.finish(); capture.begin();
    capture.message(WM_ACTIVATEAPP, FALSE, 0); CHECK(capture.cancelled());
    capture.finish(); capture.begin();
    CHECK(capture.message(WM_KEYDOWN, VK_HOME, 0) && capture.key() == VK_HOME);
    capture.finish(); CHECK(capture.suppress_home(true)); // captured HOME must not close the panel
    CHECK(capture.suppress_home(true) && !capture.suppress_home(false));
    capture.begin(); CHECK(capture.suppress_home(false)); // a new session cannot clear the active gate
    capture.cancel(); capture.finish(); CHECK(!capture.suppress_home(false));
    CHECK(ConfigKeyName(VK_F11, false) == "F11" && ConfigKeyName('Q', true) == "Q");
    CHECK(ConfigKeyName(VK_NUMPAD1, false) == "Numpad 1" && ConfigKeyName(VK_HOME, false) == "Home");
    CHECK(ConfigKeyName(0, false) == "Unbound" && ConfigKeyName(0, true) == u8"未绑定");
}
static void Hotkeys(bool reload) {
    const char* id = "verify.config.hotkeys";
    const auto path = Path(id);
    if (!reload) Write(path, "action=81\nunbound=0\ninvalid=256\nnegative=-1\nmalformed=65x\n");
    const auto owner = Add(id);
    std::vector<APH_ConfigField> fields(6);
    const char* names[]{"action", "unbound", "invalid", "negative", "malformed", "missing"};
    for (unsigned i = 0; i < fields.size(); ++i) {
        auto& f = fields[i]; f.size = sizeof(f); f.type = f.initial.type = APH_CONFIG_KEY;
        strcpy_s(f.key, names[i]); strcpy_s(f.label, names[i]); f.initial.integer = VK_F11;
    }
    fields[0].initial.integer = -1; CHECK(Register(owner, fields) == APH_E_ARGUMENT);
    fields[0].initial.integer = 256; CHECK(Register(owner, fields) == APH_E_ARGUMENT);
    fields[0].initial.integer = VK_F11; CHECK(Register(owner, fields) == APH_OK);
    std::vector<APH_ConfigValue> values; const auto state = Snapshot(owner, &values);
    for (const auto& value : values) CHECK(value.type == APH_CONFIG_KEY);
    const int savedKeys[]{VK_F12, 0, 'Q', VK_NUMPAD1, VK_CONTROL, VK_HOME};
    if (reload) {
        CHECK(!state.defaulted_fields);
        for (unsigned i = 0; i < values.size(); ++i) CHECK(values[i].integer == savedKeys[i]);
        puts("PASS hotkey persistence: fresh process restores names' underlying bindings and unbound values");
        return;
    }
    CHECK(state.defaulted_fields == 3 && values[0].integer == 'Q' && !values[1].integer);
    for (unsigned i = 2; i < values.size(); ++i) CHECK(values[i].integer == VK_F11);
    auto bad = values; bad[0].integer = -1; CHECK(api.save(owner, state.revision, 6, bad.data()) == APH_E_ARGUMENT);
    bad[0].integer = 256; CHECK(api.save(owner, state.revision, 6, bad.data()) == APH_E_ARGUMENT);
    bad[0] = values[0]; bad[0].type = APH_CONFIG_INT;
    CHECK(api.save(owner, state.revision, 6, bad.data()) == APH_E_ARGUMENT);
    CHECK(Snapshot(owner).revision == state.revision);
    for (unsigned i = 0; i < values.size(); ++i) values[i].integer = savedKeys[i];
    Save(owner, state.revision, values); CHECK(Done(owner).revision == state.revision + 1);
    const auto stored = Read(path);
    CHECK(stored.find("action=123\n") != std::string::npos && stored.find("unbound=0\n") != std::string::npos);
    CHECK(stored.find("invalid=81\n") != std::string::npos && stored.find("missing=36\n") != std::string::npos);
    KeyCaptureChecks();
    puts("PASS hotkeys: legacy numeric files, validation, key names, message capture, repeat/focus/cancel and HOME release");
}
int main(int argc, char** argv) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); *wcsrchr(exe, L'\\') = 0; directory = exe;
    CHECK(hub::initialize() == APH_OK); CHECK(argc == 2);
    if (!strcmp(argv[1], "behavior")) Behavior(); else if (!strcmp(argv[1], "persistence")) Persistence();
    else if (!strcmp(argv[1], "corrupt")) Corrupt(); else if (!strcmp(argv[1], "plugin")) Plugin();
    else if (!strcmp(argv[1], "notifications")) Notifications();
    else if (!strcmp(argv[1], "hotkeys")) Hotkeys(false);
    else if (!strcmp(argv[1], "hotkeys-reload")) Hotkeys(true); else return 2;
    printf("PASS config checks: %u assertions\n", checks.load()); return 0;
}
