#pragma once
#include "common/config.hpp"
#include "common/telemetry_store.hpp"
#include <atomic>
namespace dji_power {
class MonitorManager {
public:
    static MonitorManager& Instance();
    void Start();
    void Reconfigure(const PluginConfig& config);
    void UpdatePolicy(const PluginConfig& config);
    void ConnectNow();
    TelemetrySnapshot Snapshot() const;
private:
    std::atomic<ConnectionMode> mode_{ConnectionMode::ble};
};
} // namespace dji_power
