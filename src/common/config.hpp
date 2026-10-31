// 本文件定义插件本地配置及使用 Windows DPAPI 保护 pair_key 的持久化接口。
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dji_power {
enum class ConnectionMode { ble, cloud };
struct PluginConfig {
    ConnectionMode mode{ConnectionMode::ble};
    std::string cloud_token;
    std::wstring cloud_serial;
    std::wstring cloud_api_host{L"home-api-vg.djigate.com"};
    std::uint64_t bluetooth_address{};
    std::wstring device_name;
    std::string pair_key;
    std::vector<std::string> pair_keys;
    bool auto_connect{true};
    bool auto_reconnect{true};
};
std::wstring ModuleDirectory();
PluginConfig LoadConfig();
bool SaveConfig(const PluginConfig& config);
bool IsValidPairKey(const std::string& value);
} // namespace dji_power

