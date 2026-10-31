// 本文件以支持 DPI 和 ClearType 的原生 Win32 控件实现 DJI Power 插件设置窗口。
#include "ui/options_dialog.hpp"
#include "ble/ble_manager.hpp"
#include "cloud/pair_key_provider.hpp"
#include "ui/account_login_dialog.hpp"
#include "common/config.hpp"
#include "common/monitor_manager.hpp"
#include "cloud/cloud_protocol.hpp"
#include "protocol/duml.hpp"

#include <commctrl.h>
#include <uxtheme.h>
#include <array>
#include <algorithm>
#include <string>
#include <vector>
#include <winrt/base.h>

extern HMODULE g_module;

namespace dji_power {
namespace {
enum : int {
    IDC_TITLE = 100,
    IDC_SUBTITLE,
    IDC_BLE_MODE,
    IDC_CLOUD_MODE,
    IDC_DEVICE,
    IDC_SCAN,
    IDC_PAIR_KEY,
    IDC_DELETE_KEY,
    IDC_ADD_KEY,
    IDC_GET_KEY,
    IDC_AUTO_CONNECT,
    IDC_AUTO_RECONNECT,
    IDC_CLEAR,
    IDC_STATUS,
    IDC_VALUES,
    IDC_TEST,
    IDC_KEY_INPUT,
    IDC_CLOUD_SN,
    IDC_CLOUD_TOKEN,
    IDC_CLOUD_REGION,
    IDC_DEVICE_LABEL,
    IDC_CREDENTIAL_LABEL
};

class KeyInputWindow {
public:
    std::string Show(HWND parent) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = g_module;
        wc.lpszClassName = L"DJIPowerKeyInputWindow";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, wc.lpszClassName, L"添加 Pair Key",
            WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT,
            430, 150, parent, nullptr, g_module, this);
        if (!hwnd_) return {};
        EnableWindow(parent, FALSE);
        RECT parent_rect{}, window_rect{};
        GetWindowRect(parent, &parent_rect);
        GetWindowRect(hwnd_, &window_rect);
        SetWindowPos(hwnd_, nullptr,
            parent_rect.left + ((parent_rect.right - parent_rect.left) - (window_rect.right - window_rect.left)) / 2,
            parent_rect.top + ((parent_rect.bottom - parent_rect.top) - (window_rect.bottom - window_rect.top)) / 2,
            0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(hwnd_, SW_SHOW);
        SetFocus(input_);
        MSG message{};
        while (IsWindow(hwnd_) && GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        return key_;
    }
private:
    HWND hwnd_{};
    HWND input_{};
    HFONT font_{};
    HFONT bold_font_{};
    UINT dpi_{96};
    std::string key_;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<KeyInputWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<KeyInputWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wp, lp);
        if (message == WM_CREATE) {
            self->dpi_ = GetDpiForWindow(hwnd);
            const auto scale = [&](int value) { return MulDiv(value, static_cast<int>(self->dpi_), 96); };
            const auto create_font = [&](int weight) {
                return CreateFontW(-MulDiv(9, static_cast<int>(self->dpi_), 72), 0, 0, 0, weight,
                    FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            };
            self->font_ = create_font(FW_NORMAL);
            self->bold_font_ = create_font(FW_SEMIBOLD);
            RECT desired{0, 0, scale(440), scale(150)};
            AdjustWindowRectExForDpi(&desired, WS_CAPTION | WS_SYSMENU | WS_POPUP,
                FALSE, WS_EX_DLGMODALFRAME, self->dpi_);
            SetWindowPos(hwnd, nullptr, 0, 0, desired.right - desired.left, desired.bottom - desired.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            const auto add = [&](const wchar_t* type, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font = nullptr) {
                const auto c = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, scale(x), scale(y), scale(w), scale(h), hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_module, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : self->font_), TRUE);
                SetWindowTheme(c, L"Explorer", nullptr);
                return c;
            };
            add(L"BUTTON", L"Pair Key", BS_GROUPBOX, 12, 10, 416, 82, 0, self->bold_font_);
            add(L"STATIC", L"请输入 32 位十六进制 Pair Key：", SS_LEFT, 28, 29, 380, 20, 0);
            self->input_ = add(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 28, 53, 380, 24, IDC_KEY_INPUT);
            SendMessageW(self->input_, EM_SETLIMITTEXT, 32, 0);
            add(L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP, 260, 108, 76, 27, IDOK);
            add(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP, 344, 108, 76, 27, IDCANCEL);
            SetFocus(self->input_);
            return 0;
        }
        if (message == WM_COMMAND && LOWORD(wp) == IDOK && HIWORD(wp) == BN_CLICKED) {
            std::array<char, 64> key{};
            GetWindowTextA(self->input_, key.data(), static_cast<int>(key.size()));
            try { self->key_ = duml::NormalizePairKey(key.data()); }
            catch (...) { MessageBoxW(hwnd, L"Pair Key 必须是 32 个十六进制字符。", L"DJI Power", MB_ICONWARNING); return 0; }
            DestroyWindow(hwnd); return 0;
        }
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wp) == IDCANCEL && HIWORD(wp) == BN_CLICKED)) { DestroyWindow(hwnd); return 0; }
        if (message == WM_CTLCOLORSTATIC) {
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        if (message == WM_DESTROY) {
            DeleteObject(self->font_);
            DeleteObject(self->bold_font_);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
};

class OptionsWindow {
public:
    explicit OptionsWindow(HWND parent) : parent_(parent), original_(LoadConfig()), editing_(original_) {}

    bool Show() {
        WNDCLASSW window_class{};
        window_class.lpfnWndProc = WndProc;
        window_class.hInstance = g_module;
        window_class.lpszClassName = L"DJIPowerOptionsWindowV2";
        window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
        window_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        RegisterClassW(&window_class);

        constexpr DWORD style = WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN;
        hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, window_class.lpszClassName,
            L"DJI Power 设置", style, CW_USEDEFAULT, CW_USEDEFAULT, 680, 550,
            parent_, nullptr, g_module, this);
        if (!hwnd_) return false;

        RECT rectangle{};
        GetWindowRect(hwnd_, &rectangle);
        const int width = rectangle.right - rectangle.left;
        const int height = rectangle.bottom - rectangle.top;
        SetWindowPos(hwnd_, nullptr,
            (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
            (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
            0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);

        if (parent_) EnableWindow(parent_, FALSE);
        MSG message{};
        while (IsWindow(hwnd_) && GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (parent_) {
            EnableWindow(parent_, TRUE);
            SetForegroundWindow(parent_);
        }
        return changed_;
    }

private:
    HWND parent_{};
    HWND hwnd_{};
    HWND device_{};
    HWND pair_{};
    HWND status_{};
    HWND values_{};
    HWND cloud_serial_{};
    HWND cloud_token_{};
    HWND cloud_region_{};
    HFONT font_{};
    HFONT title_font_{};
    HFONT bold_font_{};
    UINT dpi_{96};
    PluginConfig original_;
    PluginConfig editing_;
    std::vector<DiscoveredDevice> devices_;
    std::size_t known_device_count_{static_cast<std::size_t>(-1)};
    bool changed_{};
    bool accepted_{};
    bool device_cleared_{};

    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto* self = reinterpret_cast<OptionsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<OptionsWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        return self ? self->Handle(message, wp, lp) : DefWindowProcW(hwnd, message, wp, lp);
    }

    HWND Add(const wchar_t* type, const wchar_t* text, DWORD style,
             int x, int y, int width, int height, int id = 0, HFONT font = nullptr) {
        const auto control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
            Scale(x), Scale(y), Scale(width), Scale(height), hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_module, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : font_), TRUE);
        SetWindowTheme(control, L"Explorer", nullptr);
        return control;
    }

    void SetComboHeight(HWND combo, int height) {
        // 调整折叠状态的选择区域，并为下拉列表保留适合字体的行高。
        RECT rect{};
        GetWindowRect(combo, &rect);
        const auto item_height = static_cast<int>(SendMessageW(combo, CB_GETITEMHEIGHT, static_cast<WPARAM>(-1), 0));
        const auto adjusted_height = std::max(1, item_height + Scale(height) - static_cast<int>(rect.bottom - rect.top));
        SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), adjusted_height);
        SendMessageW(combo, CB_SETITEMHEIGHT, 0, Scale(20));
    }

    void CreateFonts() {
        // 显式使用 Segoe UI 和 ClearType，避免默认 SYSTEM_FONT 导致中文显示锯齿。
        const auto create = [&](int points, int weight) {
            return CreateFontW(-MulDiv(points, static_cast<int>(dpi_), 72), 0, 0, 0,
                weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                L"Segoe UI");
        };
        font_ = create(9, FW_NORMAL);
        bold_font_ = create(9, FW_SEMIBOLD);
        title_font_ = create(9, FW_SEMIBOLD);
    }

    void Build() {
        dpi_ = GetDpiForWindow(hwnd_);
        CreateFonts();

        // Windows 8.1 时期的桌面设置窗口强调紧凑、对齐和信息密度。
        RECT desired{0, 0, Scale(570), Scale(360)};
        AdjustWindowRectExForDpi(&desired, WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_CLIPCHILDREN,
            FALSE, WS_EX_DLGMODALFRAME, dpi_);
        SetWindowPos(hwnd_, nullptr, 0, 0, desired.right - desired.left,
            desired.bottom - desired.top, SWP_NOMOVE | SWP_NOZORDER);

        Add(L"BUTTON", L"连接方式", BS_GROUPBOX, 12, 10, 546, 50, 0, bold_font_);
        const auto ble_mode = Add(L"BUTTON", L"本地 BLE", BS_AUTORADIOBUTTON | WS_GROUP,
            28, 29, 110, 22, IDC_BLE_MODE);
        const auto cloud_mode = Add(L"BUTTON", L"DJI Cloud",
            BS_AUTORADIOBUTTON, 158, 29, 215, 22, IDC_CLOUD_MODE);
        SendMessageW(ble_mode, BM_SETCHECK, editing_.mode == ConnectionMode::ble ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(cloud_mode, BM_SETCHECK, editing_.mode == ConnectionMode::cloud ? BST_CHECKED : BST_UNCHECKED, 0);

        Add(L"BUTTON", L"设备与凭据", BS_GROUPBOX, 12, 66, 546, 156, 0, bold_font_);
        Add(L"STATIC", L"设备：", SS_LEFT, 28, 96, 70, 20, IDC_DEVICE_LABEL);
        device_ = Add(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL,
            105, 91, 335, 180, IDC_DEVICE);
        SetComboHeight(device_, 26);
        Add(L"BUTTON", L"重新扫描", BS_PUSHBUTTON | WS_TABSTOP, 449, 91, 95, 26, IDC_SCAN);

        Add(L"STATIC", L"Pair Key：", SS_LEFT, 28, 132, 70, 20, IDC_CREDENTIAL_LABEL);
        pair_ = Add(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL,
            105, 127, 267, 160, IDC_PAIR_KEY);
        SetComboHeight(pair_, 26);
        Add(L"BUTTON", L"×", BS_PUSHBUTTON | WS_TABSTOP, 381, 127, 26, 26, IDC_DELETE_KEY);
        Add(L"BUTTON", L"+", BS_PUSHBUTTON | WS_TABSTOP, 414, 127, 26, 26, IDC_ADD_KEY);
        Add(L"BUTTON", L"账号登录", BS_PUSHBUTTON | WS_TABSTOP, 449, 127, 95, 26, IDC_GET_KEY);

        Add(L"STATIC", L"登录信息和临时 Token 仅保存在内存中，获取完成后立即清除。",
            SS_LEFT, 105, 162, 439, 18, IDC_SUBTITLE);
        cloud_serial_ = Add(L"EDIT", editing_.cloud_serial.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
            105, 91, 335, 26, IDC_CLOUD_SN);
        const std::wstring token_text(editing_.cloud_token.begin(), editing_.cloud_token.end());
        cloud_token_ = Add(L"EDIT", token_text.c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
            105, 127, 335, 26, IDC_CLOUD_TOKEN);
        SendMessageW(cloud_serial_, EM_SETLIMITTEXT, 64, 0);
        SendMessageW(cloud_token_, EM_SETLIMITTEXT, 16384, 0);
        cloud_region_ = Add(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP,
            105, 158, 439, 140, IDC_CLOUD_REGION);
        const wchar_t* regions[] = {L"海外 VG · home-api-vg.djigate.com", L"国内 HZ · home-api-hz.djigate.com", L"默认 · home-api.djigate.com"};
        for (const auto* region : regions) SendMessageW(cloud_region_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(region));
        const int region = editing_.cloud_api_host == L"home-api-hz.djigate.com" ? 1 : editing_.cloud_api_host == L"home-api.djigate.com" ? 2 : 0;
        SendMessageW(cloud_region_, CB_SETCURSEL, region, 0);

        const auto auto_connect = Add(L"BUTTON", L"启动后自动连接",
            BS_AUTOCHECKBOX | WS_TABSTOP, 28, 190, 155, 22, IDC_AUTO_CONNECT);
        const auto auto_reconnect = Add(L"BUTTON", L"断线自动重连",
            BS_AUTOCHECKBOX | WS_TABSTOP, 205, 190, 150, 22, IDC_AUTO_RECONNECT);
        Add(L"BUTTON", L"清空设备和 Key", BS_PUSHBUTTON | WS_TABSTOP, 374, 188, 170, 26, IDC_CLEAR);
        SendMessageW(auto_connect, BM_SETCHECK, editing_.auto_connect ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(auto_reconnect, BM_SETCHECK, editing_.auto_reconnect ? BST_CHECKED : BST_UNCHECKED, 0);

        Add(L"BUTTON", L"运行状态", BS_GROUPBOX, 12, 230, 546, 66, 0, bold_font_);
        status_ = Add(L"STATIC", L"正在启动扫描…", SS_LEFT, 28, 251, 510, 20, IDC_STATUS, bold_font_);
        values_ = Add(L"STATIC", L"输入  -- W      输出  -- W      净功率  -- W      电量  -- %",
            SS_LEFT, 28, 272, 510, 20, IDC_VALUES);

        Add(L"BUTTON", L"测试连接", BS_PUSHBUTTON | WS_TABSTOP,
            300, 313, 82, 27, IDC_TEST);
        Add(L"BUTTON", L"确定", BS_DEFPUSHBUTTON | WS_TABSTOP,
            390, 313, 76, 27, IDOK);
        Add(L"BUTTON", L"取消", BS_PUSHBUTTON | WS_TABSTOP,
            474, 313, 76, 27, IDCANCEL);

        RefreshKeys(editing_.pair_key);
        RefreshDevices();
        UpdateMode();
        SetTimer(hwnd_, 1, 400, nullptr);
    }

    void UpdateMode() {
        const bool cloud = editing_.mode == ConnectionMode::cloud;
        for (const auto id : {IDC_DEVICE, IDC_PAIR_KEY, IDC_DELETE_KEY, IDC_ADD_KEY, IDC_SUBTITLE})
            ShowWindow(GetDlgItem(hwnd_, id), cloud ? SW_HIDE : SW_SHOW);
        for (const auto id : {IDC_CLOUD_SN, IDC_CLOUD_TOKEN, IDC_CLOUD_REGION})
            ShowWindow(GetDlgItem(hwnd_, id), cloud ? SW_SHOW : SW_HIDE);
        SetDlgItemTextW(hwnd_, IDC_DEVICE_LABEL, cloud ? L"设备 SN：" : L"设备：");
        SetDlgItemTextW(hwnd_, IDC_CREDENTIAL_LABEL, cloud ? L"Token：" : L"Pair Key：");
        SetDlgItemTextW(hwnd_, IDC_SCAN, cloud ? L"登录选设备" : L"重新扫描");
    }

    static std::wstring ReadText(HWND control) {
        std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
        const int length = GetWindowTextW(control, text.data(), static_cast<int>(text.size()));
        text.resize(static_cast<std::size_t>(length));
        return text;
    }

    void RefreshKeys(const std::string& selected) {
        SendMessageW(pair_, CB_RESETCONTENT, 0, 0);
        int selected_index = -1;
        for (std::size_t i = 0; i < editing_.pair_keys.size(); ++i) {
            const auto& key = editing_.pair_keys[i];
            const std::wstring label(key.begin(), key.begin() + 8);
            const auto text = label + L"…";
            SendMessageW(pair_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
            if (key == selected) selected_index = static_cast<int>(i);
        }
        SendMessageW(pair_, CB_SETCURSEL, selected_index, 0);
    }
    void AddKey(const std::string& key) {
        if (std::find(editing_.pair_keys.begin(), editing_.pair_keys.end(), key) == editing_.pair_keys.end())
            editing_.pair_keys.push_back(key);
        editing_.pair_key = key;
        RefreshKeys(key);
    }

    void RefreshDevices() {
        const auto selected_address = editing_.bluetooth_address;
        devices_ = BleManager::Instance().Devices();
        known_device_count_ = devices_.size();
        SendMessageW(device_, CB_RESETCONTENT, 0, 0);
        if (devices_.empty()) {
            SendMessageW(device_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"正在扫描附近的 DJI Power…"));
            SendMessageW(device_, CB_SETCURSEL, 0, 0);
            return;
        }

        int selected = -1;
        for (std::size_t index = 0; index < devices_.size(); ++index) {
            wchar_t address[24]{};
            swprintf_s(address, L"  [%012llX]", static_cast<unsigned long long>(devices_[index].address));
            const auto label = devices_[index].name + address;
            SendMessageW(device_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            if (devices_[index].address == selected_address) selected = static_cast<int>(index);
        }
        if (selected < 0 && !device_cleared_) selected = 0;
        SendMessageW(device_, CB_SETCURSEL, selected, 0);
    }

    bool ReadForm(bool show_error, bool allow_empty_key = false) {
        editing_.auto_connect = SendDlgItemMessageW(hwnd_, IDC_AUTO_CONNECT, BM_GETCHECK, 0, 0) == BST_CHECKED;
        editing_.auto_reconnect = SendDlgItemMessageW(hwnd_, IDC_AUTO_RECONNECT, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (editing_.mode == ConnectionMode::cloud) {
            auto token = ReadText(cloud_token_);
            editing_.cloud_token = winrt::to_string(token);
            if (!token.empty()) SecureZeroMemory(token.data(), token.size() * sizeof(wchar_t));
            editing_.cloud_serial = ReadText(cloud_serial_);
            const auto region = SendMessageW(cloud_region_, CB_GETCURSEL, 0, 0);
            editing_.cloud_api_host = region == 1 ? L"home-api-hz.djigate.com" : region == 2 ? L"home-api.djigate.com" : L"home-api-vg.djigate.com";
            if (allow_empty_key && editing_.cloud_token.empty() && editing_.cloud_serial.empty()) return true;
            std::wstring error;
            if (!cloud::ValidateConfig(editing_, error)) {
                if (show_error) MessageBoxW(hwnd_, error.c_str(), L"DJI Cloud", MB_ICONWARNING);
                return false;
            }
            return true;
        }
        const auto key_index = static_cast<int>(SendMessageW(pair_, CB_GETCURSEL, 0, 0));
        if (key_index < 0 || static_cast<std::size_t>(key_index) >= editing_.pair_keys.size()) {
            if (!allow_empty_key) {
                if (show_error) MessageBoxW(hwnd_, L"请选择或添加 Pair Key。", L"DJI Power", MB_ICONWARNING);
                return false;
            }
            editing_.pair_key.clear();
        } else {
            editing_.pair_key = editing_.pair_keys[static_cast<std::size_t>(key_index)];
        }

        const auto index = static_cast<int>(SendMessageW(device_, CB_GETCURSEL, 0, 0));
        if (index < 0 || static_cast<std::size_t>(index) >= devices_.size()) {
            if (editing_.bluetooth_address == 0 && !(allow_empty_key && editing_.pair_key.empty())) {
                if (show_error) MessageBoxW(hwnd_, L"尚未发现设备。请保持电源开启并点击“重新扫描”。", L"DJI Power", MB_ICONWARNING);
                return false;
            }
        } else {
            editing_.bluetooth_address = devices_[index].address;
            editing_.device_name = devices_[index].name;
        }
        editing_.auto_connect = SendDlgItemMessageW(hwnd_, IDC_AUTO_CONNECT, BM_GETCHECK, 0, 0) == BST_CHECKED;
        editing_.auto_reconnect = SendDlgItemMessageW(hwnd_, IDC_AUTO_RECONNECT, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return true;
    }

    void FetchKey() {
        // 使用与已验证 Python 工具相同的 DJI Home 移动端登录流程。
        std::wstring token;
        const bool cloud = editing_.mode == ConnectionMode::cloud;
        const auto cloud_devices = ShowAccountLoginDialog(hwnd_, cloud ? &token : nullptr);
        if (cloud_devices.empty()) return;
        AddKey(cloud_devices.front().pair_key);
        editing_.device_name = cloud_devices.front().name;
        if (cloud) {
            editing_.cloud_token = winrt::to_string(token);
            editing_.cloud_serial = cloud_devices.front().serial_number;
            SetWindowTextW(cloud_serial_, editing_.cloud_serial.c_str());
            SetWindowTextW(cloud_token_, token.c_str());
            if (!token.empty()) SecureZeroMemory(token.data(), token.size() * sizeof(wchar_t));
            MessageBoxW(hwnd_, L"已选择云端设备。点击测试连接；确定后将用当前 Windows 用户的 DPAPI 加密保存 Token。", L"DJI Cloud", MB_ICONINFORMATION);
            return;
        }
        const auto message = L"已取得 " + std::to_wstring(cloud_devices.size()) +
            L" 台设备的凭据，当前填入：" + cloud_devices.front().name;
        MessageBoxW(hwnd_, message.c_str(), L"获取 Key 成功", MB_ICONINFORMATION);
    }

    void UpdateStatus() {
        const auto latest_devices = BleManager::Instance().Devices();
        if (latest_devices.size() != known_device_count_) RefreshDevices();

        auto value = MonitorManager::Instance().Snapshot();
        SetWindowTextW(status_, value.status.c_str());
        if (value.connection != ConnectionState::connected || value.updated_at == std::chrono::steady_clock::time_point{} ||
            std::chrono::steady_clock::now() - value.updated_at >= value.stale_after)
            value.input_w = value.output_w = value.battery_percent = -1;
        const auto field = [](int number) { return number < 0 ? std::wstring(L"--") : std::to_wstring(number); };
        const auto net = value.input_w < 0 || value.output_w < 0 ? std::wstring(L"--") : std::to_wstring(value.output_w - value.input_w);
        const auto text = L"输入  " + field(value.input_w) + L" W      输出  " +
            field(value.output_w) + L" W      净功率  " + net + L" W      电量  " +
            field(value.battery_percent) + L" %";
        SetWindowTextW(values_, text.c_str());
    }

    LRESULT Handle(UINT message, WPARAM wp, LPARAM lp) {
        switch (message) {
        case WM_CREATE:
            Build();
            return 0;
        case WM_TIMER:
            UpdateStatus();
            return 0;
        case WM_DRAWITEM: {
            const auto item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (item->CtlID != IDC_DEVICE && item->CtlID != IDC_PAIR_KEY) break;
            const bool selected = (item->itemState & ODS_SELECTED) != 0;
            FillRect(item->hDC, &item->rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
            if (item->itemID != static_cast<UINT>(-1)) {
                const auto length = SendMessageW(item->hwndItem, CB_GETLBTEXTLEN, item->itemID, 0);
                if (length != CB_ERR) {
                    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
                    SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, reinterpret_cast<LPARAM>(text.data()));
                    RECT text_rect = item->rcItem;
                    text_rect.left += Scale(3);
                    text_rect.right -= Scale(3);
                    SetBkMode(item->hDC, TRANSPARENT);
                    SetTextColor(item->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
                    const auto old_font = SelectObject(item->hDC, font_);
                    DrawTextW(item->hDC, text.c_str(), static_cast<int>(length), &text_rect,
                        DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
                    SelectObject(item->hDC, old_font);
                }
            }
            if (item->itemState & ODS_FOCUS) DrawFocusRect(item->hDC, &item->rcItem);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC: {
            const auto dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            const auto control = reinterpret_cast<HWND>(lp);
            const int id = GetDlgCtrlID(control);
            SetTextColor(dc, id == IDC_SUBTITLE ? RGB(96, 96, 96) : GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
            case IDC_BLE_MODE:
            case IDC_CLOUD_MODE:
                editing_.mode = LOWORD(wp) == IDC_CLOUD_MODE ? ConnectionMode::cloud : ConnectionMode::ble;
                UpdateMode();
                return 0;
            case IDC_DEVICE:
                if (HIWORD(wp) == CBN_SELCHANGE) device_cleared_ = false;
                return 0;
            case IDC_SCAN:
                if (editing_.mode == ConnectionMode::cloud) { FetchKey(); return 0; }
                BleManager::Instance().Rescan();
                known_device_count_ = static_cast<std::size_t>(-1);
                RefreshDevices();
                return 0;
            case IDC_GET_KEY:
                FetchKey();
                return 0;
            case IDC_ADD_KEY: {
                const auto key = KeyInputWindow().Show(hwnd_);
                if (!key.empty()) AddKey(key);
                return 0;
            }
            case IDC_DELETE_KEY: {
                const auto index = static_cast<int>(SendMessageW(pair_, CB_GETCURSEL, 0, 0));
                if (index >= 0 && static_cast<std::size_t>(index) < editing_.pair_keys.size()) {
                    editing_.pair_keys.erase(editing_.pair_keys.begin() + index);
                    editing_.pair_key = editing_.pair_keys.empty() ? std::string{} :
                        editing_.pair_keys[std::min(static_cast<std::size_t>(index), editing_.pair_keys.size() - 1)];
                    RefreshKeys(editing_.pair_key);
                }
                return 0;
            }
            case IDC_CLEAR: {
                auto cleared = original_;
                cleared.bluetooth_address = 0;
                cleared.device_name.clear();
                cleared.pair_key.clear();
                cleared.pair_keys.clear();
                cleared.cloud_token.clear();
                cleared.cloud_serial.clear();
                if (!SaveConfig(cleared)) { MessageBoxW(hwnd_, L"清空配置失败。", L"DJI Power", MB_ICONERROR); return 0; }
                original_ = cleared;
                editing_.bluetooth_address = 0;
                editing_.device_name.clear();
                editing_.pair_key.clear();
                editing_.pair_keys.clear();
                editing_.cloud_token.clear();
                editing_.cloud_serial.clear();
                SetWindowTextW(cloud_token_, L"");
                SetWindowTextW(cloud_serial_, L"");
                device_cleared_ = true;
                changed_ = true;
                RefreshKeys({});
                SendMessageW(device_, CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
                MonitorManager::Instance().Reconfigure(cleared);
                return 0;
            }
            case IDC_TEST:
                if (ReadForm(true)) {
                    auto test = editing_;
                    test.auto_connect = true;
                    MonitorManager::Instance().Reconfigure(test);
                    MonitorManager::Instance().ConnectNow();
                }
                return 0;
            case IDOK:
                if (ReadForm(true, true)) {
                    changed_ = SaveConfig(editing_);
                    if (!changed_) { MessageBoxW(hwnd_, L"保存配置失败。", L"DJI Power", MB_ICONERROR); return 0; }
                    accepted_ = true;
                    MonitorManager::Instance().Reconfigure(editing_);
                    DestroyWindow(hwnd_);
                }
                return 0;
            case IDCANCEL:
                MonitorManager::Instance().Reconfigure(original_);
                DestroyWindow(hwnd_);
                return 0;
            default:
                break;
            }
            break;
        case WM_CLOSE:
            if (!accepted_) MonitorManager::Instance().Reconfigure(original_);
            DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, 1);
            DeleteObject(font_);
            DeleteObject(title_font_);
            DeleteObject(bold_font_);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd_, message, wp, lp);
    }
};
} // namespace

bool ShowOptionsWindow(HWND parent) {
    OptionsWindow window(parent);
    return window.Show();
}
} // namespace dji_power

