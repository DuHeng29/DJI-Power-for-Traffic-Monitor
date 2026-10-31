// 加载实际 DLL 并切换设置界面；不初始化监控，不登录、不保存、不连接网络。
#include "PluginInterface.h"
#include "test_check.hpp"
#include <windows.h>
#include <string>

namespace {
bool checked = false;
HWND FindChild(HWND parent, const wchar_t* caption) {
    struct Search { const wchar_t* caption; HWND found; } search{caption, nullptr};
    EnumChildWindows(parent, [](HWND child, LPARAM value) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(value);
        wchar_t text[128]{};
        GetWindowTextW(child, text, 128);
        if (std::wstring(text) == state.caption) { state.found = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
void CALLBACK CheckWindow(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND window = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), [](HWND candidate, LPARAM value) -> BOOL {
        wchar_t name[128]{};
        GetClassNameW(candidate, name, 128);
        if (std::wstring(name) == L"DJIPowerOptionsWindowV2") {
            *reinterpret_cast<HWND*>(value) = candidate; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&window));
    if (!window) return;
    KillTimer(nullptr, timer);
    const auto ble = FindChild(window, L"本地 BLE");
    const auto cloud = FindChild(window, L"DJI Cloud");
    REQUIRE_TEST(ble && cloud && IsWindowEnabled(cloud));
    SendMessageW(ble, BM_CLICK, 0, 0);
    REQUIRE_TEST(FindChild(window, L"Pair Key：") != nullptr);
    REQUIRE_TEST(FindChild(window, L"重新扫描") != nullptr);
    SendMessageW(cloud, BM_CLICK, 0, 0);
    REQUIRE_TEST(FindChild(window, L"Token：") != nullptr);
    REQUIRE_TEST(FindChild(window, L"设备 SN：") != nullptr);
    REQUIRE_TEST(FindChild(window, L"登录选设备") != nullptr);
    int visible_edits = 0, masked_edits = 0;
    struct Counts { int& visible; int& masked; } counts{visible_edits, masked_edits};
    EnumChildWindows(window, [](HWND child, LPARAM value) -> BOOL {
        auto& state = *reinterpret_cast<Counts*>(value);
        wchar_t name[32]{};
        GetClassNameW(child, name, 32);
        if (std::wstring(name) == L"Edit" && IsWindowVisible(child)) {
            ++state.visible;
            if (GetWindowLongPtrW(child, GWL_STYLE) & ES_PASSWORD) ++state.masked;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&counts));
    REQUIRE_TEST(visible_edits == 2 && masked_edits == 1);
    SendMessageW(ble, BM_CLICK, 0, 0);
    REQUIRE_TEST(FindChild(window, L"Pair Key：") != nullptr);
    checked = true;
    // 直接销毁测试窗口，避免“取消”恢复配置的动作启动真实监控。
    DestroyWindow(window);
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    REQUIRE_TEST(argc == 2);
    const auto module = LoadLibraryW(argv[1]);
    REQUIRE_TEST(module != nullptr);
    const auto entry = reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module, "TMPluginGetInstance"));
    REQUIRE_TEST(entry != nullptr);
    const auto timer = SetTimer(nullptr, 0, 100, CheckWindow);
    REQUIRE_TEST(timer != 0);
    entry()->ShowOptionsDialog(nullptr);
    KillTimer(nullptr, timer);
    REQUIRE_TEST(checked);
    FreeLibrary(module);
    return 0;
}
