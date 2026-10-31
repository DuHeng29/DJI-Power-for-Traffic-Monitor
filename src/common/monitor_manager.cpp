// 每次只运行一个监控通道，Cloud 模式不占用本地 BLE 连接。
#include "common/monitor_manager.hpp"
#include "ble/ble_manager.hpp"
#include "cloud/cloud_manager.hpp"
namespace dji_power {
MonitorManager& MonitorManager::Instance() { static MonitorManager manager; return manager; }
void MonitorManager::Start() {
    const auto config = LoadConfig();
    mode_ = config.mode;
    if (config.mode == ConnectionMode::cloud) CloudManager::Instance().Reconfigure(config);
    else BleManager::Instance().Start();
}
void MonitorManager::Reconfigure(const PluginConfig& config) {
    if (config.mode == ConnectionMode::cloud) {
        BleManager::Instance().Stop(); CloudManager::Instance().Reconfigure(config);
    } else {
        CloudManager::Instance().Stop(); BleManager::Instance().Start(); BleManager::Instance().Reconfigure(config);
    }
    mode_ = config.mode;
}
void MonitorManager::ConnectNow() {
    if (mode_ == ConnectionMode::cloud) CloudManager::Instance().ConnectNow();
    else BleManager::Instance().ConnectNow();
}
void MonitorManager::UpdatePolicy(const PluginConfig& config) {
    if (mode_ == ConnectionMode::cloud) CloudManager::Instance().UpdatePolicy(config);
    else BleManager::Instance().UpdatePolicy(config);
}
TelemetrySnapshot MonitorManager::Snapshot() const {
    return mode_ == ConnectionMode::cloud ? CloudManager::Instance().Snapshot() : BleManager::Instance().Snapshot();
}
} // namespace dji_power
