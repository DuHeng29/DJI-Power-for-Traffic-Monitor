// 本文件把后台 BLE 快照格式化为 TrafficMonitor 可展示的五个监控项。
#include "plugin/plugin.hpp"
#include "common/monitor_manager.hpp"
#include "ui/options_dialog.hpp"
#include <windows.h>
#include <chrono>

namespace dji_power {
Plugin::Plugin() : items_{
    PowerItem(L"DJI 电量", L"dji_power_battery", L"DJI 电量", L"100 %"),
    PowerItem(L"DJI 输入", L"dji_power_input", L"DJI 输入", L"9999 W"),
    PowerItem(L"DJI 输出", L"dji_power_output", L"DJI 输出", L"9999 W"),
    PowerItem(L"DJI 净功率", L"dji_power_net", L"DJI 净功率", L"-9999 W"),
    PowerItem(L"DJI 剩余", L"dji_power_runtime", L"DJI 剩余", L"99:59") } {}

Plugin& Plugin::Instance() { static Plugin instance; return instance; }
IPluginItem* Plugin::GetItem(int index) { return index >= 0 && index < static_cast<int>(items_.size()) ? &items_[index] : nullptr; }

void Plugin::DataRequired() {
    const auto snapshot = MonitorManager::Instance().Snapshot();
    const bool fresh = snapshot.connection == ConnectionState::connected && snapshot.updated_at != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() - snapshot.updated_at < snapshot.stale_after;
    if (!fresh) {
        for (auto& item : items_) item.SetValue(L"--");
    } else {
        const auto field = [](int value, const wchar_t* unit) { return value < 0 ? std::wstring(L"--") : std::to_wstring(value) + unit; };
        items_[0].SetValue(field(snapshot.battery_percent, L" %"));
        items_[1].SetValue(field(snapshot.input_w, L" W"));
        items_[2].SetValue(field(snapshot.output_w, L" W"));
        items_[3].SetValue(snapshot.output_w < 0 || snapshot.input_w < 0 ? L"--" : std::to_wstring(snapshot.output_w - snapshot.input_w) + L" W");
        wchar_t runtime[16]{};
        swprintf_s(runtime, L"%d:%02d", snapshot.runtime_min / 60, snapshot.runtime_min % 60);
        items_[4].SetValue(snapshot.runtime_min < 0 ? L"--" : runtime);
    }
    tooltip_ = L"DJI Power\n状态：" + snapshot.status;
    if (fresh) tooltip_ += L"\n输入：" + std::to_wstring(snapshot.input_w) + L" W  输出：" + std::to_wstring(snapshot.output_w) + L" W";
}

const wchar_t* Plugin::GetInfo(PluginInfoIndex index) {
    switch (index) {
    case TMI_NAME: return L"DJI Power for TrafficMonitor";
    case TMI_DESCRIPTION: return L"通过本地 BLE 或 DJI Cloud 显示 DJI Power 电量与功率";
    case TMI_AUTHOR: return L"DuHeng29";
    case TMI_COPYRIGHT: return L"Copyright (c) 2026 DuHeng29";
    case TMI_VERSION: return L"0.4.0";
    case TMI_URL: return L"https://github.com/DuHeng29/DJI-Power-for-Traaffic-Monitor";
    default: return L"";
    }
}
ITMPlugin::OptionReturn Plugin::ShowOptionsDialog(void* parent) {
    return ShowOptionsWindow(static_cast<HWND>(parent)) ? OR_OPTION_CHANGED : OR_OPTION_UNCHANGED;
}
void Plugin::OnInitialize(ITrafficMonitor*) { MonitorManager::Instance().Start(); }
const wchar_t* Plugin::GetTooltipInfo() { return tooltip_.c_str(); }
} // namespace dji_power

ITMPlugin* TMPluginGetInstance() { return &dji_power::Plugin::Instance(); }

