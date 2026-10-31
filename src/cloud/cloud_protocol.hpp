// DJI Cloud REST 数据与 MQTT 3.1.1 编解码，不进行网络操作。
#pragma once
#include "common/config.hpp"
#include "protocol/duml.hpp"
#include <span>
#include <string_view>
#include <stdexcept>

namespace dji_power::cloud {
class AuthenticationError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct MqttCredentials {
    std::string domain, client_id, username, password;
    int port{8883};
    int lifetime_seconds{14400};
};
bool ValidateConfig(const PluginConfig& config, std::wstring& error);
MqttCredentials ParseCredentials(std::string_view json);
std::optional<duml::Report> ParseTelemetry(std::string_view json);
std::string PropertyTopic(std::wstring_view serial);
using Bytes = std::vector<std::uint8_t>;
Bytes ConnectPacket(const MqttCredentials& credentials);
Bytes SubscribePacket(std::string_view topic);
struct MqttPacket { std::uint8_t header; Bytes body; };
class MqttDecoder {
public:
    std::vector<MqttPacket> Feed(std::span<const std::uint8_t> bytes);
private:
    Bytes buffer_;
};
struct Publication { std::string topic, payload; std::uint16_t packet_id{}; int qos{}; };
Publication ParsePublication(const MqttPacket& packet);
} // namespace dji_power::cloud
