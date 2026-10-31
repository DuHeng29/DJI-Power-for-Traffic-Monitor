// 不提供真实 Token，不发起网络连接，验证手工连接模式及停止/重配生命周期。
#include "cloud/cloud_manager.hpp"
#include "test_check.hpp"
#include <windows.h>
#include <chrono>
#include <thread>

HMODULE g_module = nullptr;
int main() {
    using namespace std::chrono_literals;
    auto& manager = dji_power::CloudManager::Instance();
    dji_power::PluginConfig config;
    config.mode = dji_power::ConnectionMode::cloud;
    config.cloud_token = "CN_offline-test-token";
    config.cloud_serial = L"TEST123";
    config.auto_connect = false;
    manager.Reconfigure(config);
    std::this_thread::sleep_for(100ms);
    REQUIRE_TEST(manager.Snapshot().connection == dji_power::ConnectionState::stopped);
    const auto start = std::chrono::steady_clock::now();
    manager.Stop();
    REQUIRE_TEST(std::chrono::steady_clock::now() - start < 3s);
    manager.Stop();
    config.auto_connect = true;
    config.cloud_api_host = L"example.com";
    manager.Reconfigure(config); // 非官方地址必须在启动网络线程前被拒绝。
    REQUIRE_TEST(manager.Snapshot().connection == dji_power::ConnectionState::error);
    manager.Stop();
    return 0;
}
