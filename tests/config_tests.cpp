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
    {
        auto edited = config;
        edited.device_name = L"Renamed";
        edited.cloud_tokens.push_back("US_another-history-token");
        edited.cloud_serials.push_back(L"ANOTHER_SN");
        edited.pair_key = "abcdef0123456789abcdef0123456789";
        edited.bluetooth_address = 1;
        REQUIRE_TEST(dji_power::SameConnectionSettings(config, edited));
        edited.auto_connect = !config.auto_connect;
        edited.auto_reconnect = !config.auto_reconnect;
        REQUIRE_TEST(!dji_power::SameConnectionSettings(config, edited));
        REQUIRE_TEST(dji_power::SameConnectionTarget(config, edited));
        edited = config;
        edited.cloud_token += "changed";
        REQUIRE_TEST(!dji_power::SameConnectionTarget(config, edited));
        edited = config;
        edited.cloud_serial = L"OTHER_SN";
        REQUIRE_TEST(!dji_power::SameConnectionTarget(config, edited));
        edited = config;
        edited.cloud_api_host = L"home-api-vg.djigate.com";
        REQUIRE_TEST(!dji_power::SameConnectionTarget(config, edited));
        edited = config;
        edited.mode = dji_power::ConnectionMode::ble;
        REQUIRE_TEST(!dji_power::SameConnectionTarget(config, edited));
        auto ble = edited;
        ble.pair_key = "ABCDEF0123456789ABCDEF0123456789";
        edited.pair_key = "abcdef0123456789abcdef0123456789";
        edited.cloud_token = "US_unused-token";
        REQUIRE_TEST(dji_power::SameConnectionSettings(ble, edited));
        edited.bluetooth_address = 42;
        REQUIRE_TEST(!dji_power::SameConnectionTarget(ble, edited));
        edited = ble;
        edited.pair_key = "00000000000000000000000000000000";
        REQUIRE_TEST(!dji_power::SameConnectionTarget(ble, edited));
    }
    config.cloud_serials = {config.cloud_serial, L"TEST456"};
    config.cloud_tokens = {config.cloud_token};
    for (int index = 0; index < 100; ++index)
        config.cloud_tokens.push_back("US_offline-history-token-" + std::to_string(index));
    for (int index = 0; index < 200; ++index) {
        char key[33]{};
        sprintf_s(key, "%032x", index);
        config.pair_keys.emplace_back(key);
    }
    config.pair_key = config.pair_keys[49];
    const bool saved = dji_power::SaveConfig(config);
    const auto loaded = dji_power::LoadConfig();
    // 删除新增历史字段，模拟升级前只有单条 SN/Token 的旧配置。
    REQUIRE_TEST(WritePrivateProfileStringW(L"connection", L"cloud_tokens_dpapi", nullptr, path.c_str()));
    REQUIRE_TEST(WritePrivateProfileStringW(L"connection", L"cloud_serials", nullptr, path.c_str()));
    const auto migrated = dji_power::LoadConfig();
    REQUIRE_TEST(migrated.cloud_tokens == std::vector<std::string>{config.cloud_token});
    REQUIRE_TEST(migrated.cloud_serials == std::vector<std::wstring>{config.cloud_serial});
    // 重新保存空配置，验证“清空”不会把历史凭据重新带回来。
    REQUIRE_TEST(dji_power::SaveConfig(dji_power::PluginConfig{}));
    const auto cleared = dji_power::LoadConfig();
    REQUIRE_TEST(cleared.cloud_tokens.empty() && cleared.cloud_serials.empty());
    REQUIRE_TEST(cleared.cloud_token.empty() && cleared.cloud_serial.empty());
    REQUIRE_TEST(dji_power::SaveConfig(config));
    std::ifstream file(path, std::ios::binary);
    const std::string on_disk((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    // 先移除测试文件，确保后续检查失败时也不会遗留配置影响下次运行。
    const bool removed = std::filesystem::remove(path);
    REQUIRE_TEST(saved);
    REQUIRE_TEST(on_disk.find(config.cloud_token) == std::string::npos);
    REQUIRE_TEST(on_disk.find("offline-history-token") == std::string::npos);
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
    REQUIRE_TEST(loaded.cloud_tokens == config.cloud_tokens);
    REQUIRE_TEST(loaded.cloud_serials == config.cloud_serials);
    return 0;
}
