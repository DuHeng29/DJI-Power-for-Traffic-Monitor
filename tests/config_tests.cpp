// 在独立测试输出目录中验证 DPAPI 配置往返，不访问插件的配置文件。
#include "common/config.hpp"
#include "test_check.hpp"
#include <windows.h>
#include <filesystem>
#include <cstdio>
#include <fstream>

HMODULE g_module = nullptr;

int main() {
    const auto path = std::filesystem::path(dji_power::ModuleDirectory()) / L"config.ini";
    // 拒绝覆盖已有配置，即使测试被手工复制到插件目录也不会破坏数据。
    REQUIRE_TEST(!std::filesystem::exists(path));
    const auto defaults = dji_power::LoadConfig();
    REQUIRE_TEST(defaults.mode == dji_power::ConnectionMode::ble && defaults.cloud_token.empty());
    dji_power::PluginConfig config;
    config.bluetooth_address = 0x123456789ABC;
    config.device_name = L"DJI Power 测试";
    config.auto_connect = false;
    config.auto_reconnect = false;
    config.mode = dji_power::ConnectionMode::cloud;
    config.cloud_token = "CN_offline-test-token";
    config.cloud_serial = L"TEST123";
    config.cloud_api_host = L"home-api-hz.djigate.com";
    for (int index = 0; index < 200; ++index) {
        char key[33]{};
        sprintf_s(key, "%032x", index);
        config.pair_keys.emplace_back(key);
    }
    config.pair_key = config.pair_keys[49];
    const bool saved = dji_power::SaveConfig(config);
    const auto loaded = dji_power::LoadConfig();
    std::ifstream file(path, std::ios::binary);
    const std::string on_disk((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    // 先移除测试文件，确保后续检查失败时也不会遗留配置影响下次运行。
    const bool removed = std::filesystem::remove(path);
    REQUIRE_TEST(saved);
    REQUIRE_TEST(on_disk.find(config.cloud_token) == std::string::npos);
    REQUIRE_TEST(removed);
    REQUIRE_TEST(loaded.pair_keys == config.pair_keys);
    REQUIRE_TEST(loaded.pair_key == config.pair_key);
    REQUIRE_TEST(loaded.bluetooth_address == config.bluetooth_address);
    REQUIRE_TEST(loaded.device_name == config.device_name);
    REQUIRE_TEST(!loaded.auto_connect && !loaded.auto_reconnect);
    REQUIRE_TEST(loaded.mode == dji_power::ConnectionMode::cloud);
    REQUIRE_TEST(loaded.cloud_token == config.cloud_token);
    REQUIRE_TEST(loaded.cloud_serial == config.cloud_serial);
    REQUIRE_TEST(loaded.cloud_api_host == config.cloud_api_host);
    return 0;
}
