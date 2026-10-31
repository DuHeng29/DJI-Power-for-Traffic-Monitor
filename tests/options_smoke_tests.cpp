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
        if (std::wstring(text) == state.caption && IsWindowVisible(child)) { state.found = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
HWND FindRightInRow(HWND window, HWND anchor, const wchar_t* type, const wchar_t* caption = nullptr) {
    struct Search { RECT row; const wchar_t* type; const wchar_t* caption; HWND found; } search{};
    GetWindowRect(anchor, &search.row);
    search.type = type;
    search.caption = caption;
    EnumChildWindows(window, [](HWND child, LPARAM value) -> BOOL {
        auto& state = *reinterpret_cast<Search*>(value);
        wchar_t type[32]{}, caption[128]{};
        GetClassNameW(child, type, 32);
        if (!IsWindowVisible(child) || std::wstring(type) != state.type) return TRUE;
        GetWindowTextW(child, caption, 128);
        if (state.caption && std::wstring(caption) != state.caption) return TRUE;
        RECT rect{};
        GetWindowRect(child, &rect);
        const auto center_y = (rect.top + rect.bottom) / 2;
        if (rect.left >= state.row.right && center_y >= state.row.top && center_y < state.row.bottom) {
            state.found = child; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
void CALLBACK FillToken(HWND, UINT, UINT_PTR timer, DWORD) {
    const auto dialog = FindWindowW(L"DJIPowerKeyInputWindow", L"添加 Cloud Token");
    if (!dialog) return;
    KillTimer(nullptr, timer);
    HWND input = nullptr;
    EnumChildWindows(dialog, [](HWND child, LPARAM value) -> BOOL {
        wchar_t name[32]{};
        GetClassNameW(child, name, 32);
        if (std::wstring(name) == L"Edit") { *reinterpret_cast<HWND*>(value) = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&input));
    REQUIRE_TEST(input != nullptr);
    REQUIRE_TEST((GetWindowLongPtrW(input, GWL_STYLE) & ES_PASSWORD) != 0);
    REQUIRE_TEST(SendMessageW(input, EM_GETLIMITTEXT, 0, 0) == 16384);
    const auto token = L"CN_" + std::wstring(512, L'a');
    SetWindowTextW(input, token.c_str());
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
}
void CALLBACK FillSerial(HWND, UINT, UINT_PTR timer, DWORD) {
    const auto dialog = FindWindowW(L"DJIPowerKeyInputWindow", nullptr);
    if (!dialog) return;
    KillTimer(nullptr, timer);
    HWND input = nullptr;
    EnumChildWindows(dialog, [](HWND child, LPARAM value) -> BOOL {
        wchar_t name[32]{};
        GetClassNameW(child, name, 32);
        if (std::wstring(name) == L"Edit") { *reinterpret_cast<HWND*>(value) = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&input));
    REQUIRE_TEST(input != nullptr);
    SetWindowTextW(input, L"TESTMANUAL123");
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
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
    REQUIRE_TEST(FindChild(window, L"账号登录") == nullptr);
    int visible_edits = 0, visible_combos = 0;
    struct Counts { int& edits; int& combos; } counts{visible_edits, visible_combos};
    EnumChildWindows(window, [](HWND child, LPARAM value) -> BOOL {
        auto& state = *reinterpret_cast<Counts*>(value);
        wchar_t name[32]{};
        GetClassNameW(child, name, 32);
        if (std::wstring(name) == L"Edit" && IsWindowVisible(child)) ++state.edits;
        if (std::wstring(name) == L"ComboBox" && IsWindowVisible(child)) ++state.combos;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&counts));
    REQUIRE_TEST(visible_edits == 0 && visible_combos == 3);
    const auto add_sn = FindChild(window, L"+");
    REQUIRE_TEST(add_sn != nullptr);
    const auto serial_timer = SetTimer(nullptr, 0, 50, FillSerial);
    REQUIRE_TEST(serial_timer != 0);
    SendMessageW(add_sn, BM_CLICK, 0, 0);
    KillTimer(nullptr, serial_timer);
    // 手工 SN 必须成为下拉列表的当前选项，而不是只更新显示文字。
    HWND selected_serial = nullptr;
    EnumChildWindows(window, [](HWND child, LPARAM value) -> BOOL {
        wchar_t name[32]{}, text[128]{};
        GetClassNameW(child, name, 32);
        GetWindowTextW(child, text, 128);
        if (std::wstring(name) == L"ComboBox" && std::wstring(text) == L"TESTMANUAL123") {
            *reinterpret_cast<HWND*>(value) = child; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&selected_serial));
    REQUIRE_TEST(selected_serial != nullptr);
    REQUIRE_TEST(SendMessageW(selected_serial, CB_GETCURSEL, 0, 0) != CB_ERR);
    const auto token_combo = FindRightInRow(window, FindChild(window, L"Token："), L"ComboBox");
    REQUIRE_TEST(token_combo != nullptr);
    const auto add_token = FindRightInRow(window, token_combo, L"Button", L"+");
    const auto delete_token = FindRightInRow(window, token_combo, L"Button", L"×");
    REQUIRE_TEST(add_token && delete_token);
    const auto original_count = SendMessageW(token_combo, CB_GETCOUNT, 0, 0);
    for (int attempt = 0; attempt < 2; ++attempt) {
        const auto token_timer = SetTimer(nullptr, 0, 50, FillToken);
        REQUIRE_TEST(token_timer != 0);
        SendMessageW(add_token, BM_CLICK, 0, 0);
        KillTimer(nullptr, token_timer);
        REQUIRE_TEST(SendMessageW(token_combo, CB_GETCOUNT, 0, 0) == original_count + 1);
        REQUIRE_TEST(SendMessageW(token_combo, CB_GETCURSEL, 0, 0) != CB_ERR);
        wchar_t label[128]{};
        GetWindowTextW(token_combo, label, 128);
        REQUIRE_TEST(std::wstring(label).find(L"Token ") == 0);
        REQUIRE_TEST(std::wstring(label).find(std::wstring(16, L'a')) == std::wstring::npos);
    }
    SendMessageW(delete_token, BM_CLICK, 0, 0);
    REQUIRE_TEST(SendMessageW(token_combo, CB_GETCOUNT, 0, 0) == original_count);
    SendMessageW(ble, BM_CLICK, 0, 0);
    REQUIRE_TEST(!IsWindowVisible(add_token) && !IsWindowVisible(delete_token));
    REQUIRE_TEST(FindChild(window, L"Pair Key：") != nullptr);
    REQUIRE_TEST(FindChild(window, L"账号登录") != nullptr);
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
    // 所有编辑都未测试、未应用：“取消”必须直接退出，不得启动或重连后台。
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
}
void CALLBACK CloseUnchanged(HWND, UINT, UINT_PTR timer, DWORD) {
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
    SendMessageW(window, WM_CLOSE, 0, 0);
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    REQUIRE_TEST(argc == 2);
    const auto module = LoadLibraryW(argv[1]);
    REQUIRE_TEST(module != nullptr);
    const auto entry = reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module, "TMPluginGetInstance"));
    REQUIRE_TEST(entry != nullptr);
    auto* plugin = entry();
    plugin->DataRequired();
    const std::wstring before = plugin->GetTooltipInfo();
    const auto timer = SetTimer(nullptr, 0, 100, CheckWindow);
    REQUIRE_TEST(timer != 0);
    plugin->ShowOptionsDialog(nullptr);
    KillTimer(nullptr, timer);
    REQUIRE_TEST(checked);
    Sleep(100);
    plugin->DataRequired();
    REQUIRE_TEST(std::wstring(plugin->GetTooltipInfo()) == before);
    const auto close_timer = SetTimer(nullptr, 0, 100, CloseUnchanged);
    REQUIRE_TEST(close_timer != 0);
    plugin->ShowOptionsDialog(nullptr);
    KillTimer(nullptr, close_timer);
    Sleep(100);
    plugin->DataRequired();
    REQUIRE_TEST(std::wstring(plugin->GetTooltipInfo()) == before);
    FreeLibrary(module);
    return 0;
}
