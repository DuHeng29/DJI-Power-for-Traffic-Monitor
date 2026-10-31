// 原生 HTTPS + TLS MQTT 云端遥测工作线程。
#pragma once
#include "common/config.hpp"
#include "common/telemetry_store.hpp"
#include <memory>
namespace dji_power {
class CloudManager {
public:
    static CloudManager& Instance();
    void Reconfigure(const PluginConfig& config);
    void UpdatePolicy(const PluginConfig& config);
    void ConnectNow();
    void Stop();
    TelemetrySnapshot Snapshot() const;
private:
    CloudManager();
    ~CloudManager();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dji_power
