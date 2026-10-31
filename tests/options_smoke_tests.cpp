// 加载实际 DLL 并切换设置界面；不初始化监控，不登录、不保存、不连接网络。
#include "PluginInterface.h"
#include "test_check.hpp"
#include <windows.h>
#include <string>

namespace {
bool checked = false;
void CheckGroupBackground(HWND window) {
    for (auto group = GetWindow(window, GW_CHILD); group; group = GetWindow(group, GW_HWNDNEXT)) {
        wchar_t type[32]{};
        GetClassNameW(group, type, 32);
        if (std::wstring(type) != L"Button" || (GetWindowLongPtrW(group, GWL_STYLE) & BS_TYPEMASK) != BS_GROUPBOX) continue;
        RECT rect{};
        GetClientRect(group, &rect);
        const auto screen = GetDC(group);
        const auto dc = CreateCompatibleDC(screen);
        const auto bitmap = CreateCompatibleBitmap(screen, rect.right, rect.bottom);
        const auto old = SelectObject(dc, bitmap);
        const auto marker = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(dc, &rect, marker);
        DeleteObject(marker);
        // 分组框覆盖父窗口的擦除区域，因此必须主动擦除内部旧控件留下的像素。
        SendMessageW(group, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0);
        const auto pixel = GetPixel(dc, rect.right / 2, rect.bottom / 2);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(group, screen);
        REQUIRE_TEST(pixel == GetSysColor(COLOR_WINDOW));
    }
}
void CheckGroupBoxOrder(HWND window) {
    // WS_VISIBLE 不代表能绘制：上层分组框的矩形会裁掉内部兄弟控件。
    for (auto child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!IsWindowVisible(child)) continue;
        wchar_t type[32]{};
        GetClassNameW(child, type, 32);
        if (std::wstring(type) == L"Button" && (GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX) continue;
        RECT rect{};
        GetWindowRect(child, &rect);
        const POINT center{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
        for (auto above = GetWindow(child, GW_HWNDPREV); above; above = GetWindow(above, GW_HWNDPREV)) {
            GetClassNameW(above, type, 32);
            if (std::wstring(type) != L"Button" || (GetWindowLongPtrW(above, GWL_STYLE) & BS_TYPEMASK) != BS_GROUPBOX) continue;
            RECT group{};
            GetWindowRect(above, &group);
            REQUIRE_TEST(!IsWindowVisible(above) || !PtInRect(&group, center));
        }
    }
}
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
    CheckGroupBoxOrder(window);
    CheckGroupBackground(window);
    const auto ble = FindChild(window, L"本地 BLE");
    const auto cloud = FindChild(window, L"DJI Cloud");
    REQUIRE_TEST(ble && cloud && IsWindowEnabled(cloud));
    SendMessageW(ble, BM_CLICK, 0, 0);
    REQUIRE_TEST(FindChild(window, L"Pair Key：") != nullptr);
    REQUIRE_TEST(FindChild(window, L"重新扫描") != nullptr);
    SendMessageW(cloud, BM_CLICK, 0, 0);
    CheckGroupBoxOrder(window);
    CheckGroupBackground(window);
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
    // 重复切换时，隐藏控件的下拉弹窗不能残留；相邻控件必须相互裁剪。
    for (int iteration = 0; iteration < 6; ++iteration) {
        HWND combo = nullptr;
        EnumChildWindows(window, [](HWND child, LPARAM value) -> BOOL {
            wchar_t name[32]{};
            GetClassNameW(child, name, 32);
            if (std::wstring(name) == L"ComboBox" && IsWindowVisible(child)) {
                *reinterpret_cast<HWND*>(value) = child; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&combo));
        REQUIRE_TEST(combo != nullptr);
        REQUIRE_TEST((GetWindowLongPtrW(combo, GWL_STYLE) & WS_CLIPSIBLINGS) != 0);
        SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
        SendMessageW(cloud, BM_CLICK, 0, 0);
        REQUIRE_TEST(SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) == FALSE);
        REQUIRE_TEST(!IsWindowVisible(combo));
        SendMessageW(ble, BM_CLICK, 0, 0);
        REQUIRE_TEST(IsWindowVisible(combo));
        CheckGroupBackground(window);
    }
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
