// 根据参考项目的 REST/MQTT 字段独立实现；不依赖 Home Assistant 或 Python。
#include "cloud/cloud_protocol.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dji_power::cloud {
namespace {
namespace json = winrt::Windows::Data::Json;
constexpr std::size_t kMaxPacket = 1024 * 1024;
[[noreturn]] void Invalid(const char* message) { throw std::runtime_error(message); }
json::JsonObject Object(const json::JsonObject& object, const wchar_t* name) {
    const auto value = object.TryLookup(name);
    return value && value.ValueType() == json::JsonValueType::Object ? value.GetObject() : json::JsonObject();
}
std::string String(const json::JsonObject& object, const wchar_t* name) {
    const auto value = object.TryLookup(name);
    return value && value.ValueType() == json::JsonValueType::String ? winrt::to_string(value.GetString()) : std::string{};
}
std::optional<double> Number(const json::JsonObject& object, const wchar_t* name) {
    const auto value = object.TryLookup(name);
    if (!value) return {};
    double number = 0;
    if (value.ValueType() == json::JsonValueType::Number) number = value.GetNumber();
    else if (value.ValueType() == json::JsonValueType::String) {
        try {
            const auto text = winrt::to_string(value.GetString());
            std::size_t end = 0;
            number = std::stod(text, &end);
            if (end != text.size()) return {};
        } catch (...) { return {}; }
    } else return {};
    return std::isfinite(number) ? std::optional<double>(number) : std::nullopt;
}
std::optional<int> Integer(const json::JsonObject& object, const wchar_t* name, int maximum) {
    const auto number = Number(object, name);
    if (!number || *number < 0 || *number > maximum) return {};
    return static_cast<int>(*number);
}
bool AsciiName(std::string_view value, std::size_t max, bool dots) {
    return !value.empty() && value.size() <= max && std::all_of(value.begin(), value.end(), [=](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || (dots ? ch == '.' : ch == '_');
    });
}
void Text(Bytes& bytes, std::string_view value) {
    if (value.size() > 65535) Invalid("MQTT field too long");
    bytes.push_back(static_cast<std::uint8_t>(value.size() >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}
Bytes Frame(std::uint8_t header, const Bytes& body) {
    if (body.size() > kMaxPacket) Invalid("MQTT packet too large");
    Bytes bytes{header};
    auto length = body.size();
    do {
        auto digit = static_cast<std::uint8_t>(length % 128);
        length /= 128;
        if (length) digit |= 128;
        bytes.push_back(digit);
    } while (length);
    bytes.insert(bytes.end(), body.begin(), body.end());
    return bytes;
}
} // namespace

bool ValidateConfig(const PluginConfig& config, std::wstring& error) {
    error.clear();
    if (config.cloud_api_host != L"home-api.djigate.com" && config.cloud_api_host != L"home-api-vg.djigate.com" &&
        config.cloud_api_host != L"home-api-hz.djigate.com") {
        error = L"请选择有效的 DJI Cloud 区域"; return false;
    }
    const auto& token = config.cloud_token;
    const auto separator = token.find('_');
    if (separator < 2 || separator > 8 || separator + 1 >= token.size() || token.size() > 16384 ||
        !std::all_of(token.begin(), token.end(), [](unsigned char ch) { return ch > 32 && ch < 127; }) ||
        !std::all_of(token.begin(), token.begin() + static_cast<std::ptrdiff_t>(separator),
            [](char ch) { return ch >= 'A' && ch <= 'Z'; })) {
        error = L"请登录账号或填写完整的 Member Token（例如 CN_…、US_…）"; return false;
    }
    if (!AsciiName(winrt::to_string(config.cloud_serial), 64, false)) {
        error = L"请填写设备序列号 SN，或通过账号登录选择设备"; return false;
    }
    return true;
}

MqttCredentials ParseCredentials(std::string_view text) {
    json::JsonObject root;
    if (!json::JsonObject::TryParse(winrt::to_hstring(text), root)) Invalid("DJI Cloud returned invalid JSON");
    const auto code = Number(Object(root, L"result"), L"code");
    if (code && *code != 0) {
        if (*code == 121010 || *code == 100401 || *code == 401 || *code == 403)
            throw AuthenticationError("DJI Cloud token expired; sign in again");
        Invalid("DJI Cloud API rejected credential request");
    }
    const auto data = root.HasKey(L"data") ? Object(root, L"data") : root;
    MqttCredentials result;
    result.domain = String(data, L"mqtt_domain");
    std::transform(result.domain.begin(), result.domain.end(), result.domain.begin(), [](unsigned char ch) {
        return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch);
    });
    if (!result.domain.ends_with(".djigate.com") && !result.domain.ends_with(".dji.com"))
        Invalid("MQTT broker is not an allowed DJI domain");
    result.client_id = String(data, L"client_id");
    result.username = String(data, L"user_uuid");
    result.password = String(data, L"user_token");
    result.port = Integer(data, L"mqtt_port", 65535).value_or(0);
    result.lifetime_seconds = Integer(data, L"expire", 86400).value_or(14400);
    if (!AsciiName(result.domain, 253, true) || result.port == 0 || result.client_id.empty() ||
        result.username.empty() || result.password.empty() || result.lifetime_seconds < 1 ||
        result.client_id.size() > 65535 || result.username.size() > 65535 || result.password.size() > 65535)
        Invalid("DJI Cloud returned incomplete MQTT credentials");
    return result;
}

std::optional<duml::Report> ParseTelemetry(std::string_view text) {
    if (text.size() > kMaxPacket) return {};
    json::JsonObject root;
    if (!json::JsonObject::TryParse(winrt::to_hstring(text), root) || String(root, L"method") != "device_osd") return {};
    const auto host = Object(Object(root, L"data"), L"host");
    const auto battery = Object(host, L"battery");
    const auto power = Object(host, L"power_info");
    duml::Report report;
    if (const auto percent = Number(battery, L"charge_pct"); percent && *percent >= 0 && *percent <= 10000)
        report.battery_percent = static_cast<int>(*percent / 100);
    report.runtime_min = Integer(battery, L"remain_time", 1000000);
    report.input_w = Integer(power, L"input", 1000000);
    report.output_w = Integer(power, L"output", 1000000);
    if (!report.battery_percent && !report.runtime_min && !report.input_w && !report.output_w) return {};
    return report;
}

std::string PropertyTopic(std::wstring_view serial) {
    const auto sn = winrt::to_string(serial);
    if (!AsciiName(sn, 64, false)) Invalid("Invalid DJI Power serial number");
    return "forward/dy/thing/product/" + sn + "/property";
}
Bytes ConnectPacket(const MqttCredentials& credentials) {
    Bytes body{0, 4, 'M', 'Q', 'T', 'T', 4, 0xC2, 0, 60};
    Text(body, credentials.client_id);
    Text(body, credentials.username);
    Text(body, credentials.password);
    return Frame(0x10, body);
}
Bytes SubscribePacket(std::string_view topic) {
    Bytes body{0, 1};
    Text(body, topic);
    body.push_back(0);
    return Frame(0x82, body);
}
std::vector<MqttPacket> MqttDecoder::Feed(std::span<const std::uint8_t> bytes) {
    if (buffer_.size() + bytes.size() > kMaxPacket + 5) Invalid("MQTT receive buffer exceeded");
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    std::vector<MqttPacket> packets;
    while (buffer_.size() >= 2) {
        std::size_t length = 0, multiplier = 1, offset = 1;
        bool complete = false;
        for (int i = 0; i < 4 && offset < buffer_.size(); ++i) {
            const auto digit = buffer_[offset++];
            length += (digit & 127) * multiplier;
            if (length > kMaxPacket) Invalid("MQTT packet too large");
            if (!(digit & 128)) { complete = true; break; }
            if (i == 3) Invalid("Invalid MQTT remaining length");
            multiplier *= 128;
        }
        if (!complete || buffer_.size() < offset + length) break;
        if ((buffer_[0] >> 4) == 0 || (buffer_[0] >> 4) == 15) Invalid("Invalid MQTT packet type");
        packets.push_back({buffer_[0], Bytes(buffer_.begin() + offset, buffer_.begin() + offset + length)});
        buffer_.erase(buffer_.begin(), buffer_.begin() + offset + length);
    }
    return packets;
}
Publication ParsePublication(const MqttPacket& packet) {
    if ((packet.header >> 4) != 3 || packet.body.size() < 2) Invalid("Invalid MQTT publication");
    const auto length = (static_cast<std::size_t>(packet.body[0]) << 8) | packet.body[1];
    std::size_t offset = length + 2;
    if (!length || offset > packet.body.size()) Invalid("Invalid MQTT topic length");
    Publication result;
    result.qos = (packet.header >> 1) & 3;
    result.topic.assign(packet.body.begin() + 2, packet.body.begin() + offset);
    if (result.qos > 1) Invalid("Unsupported MQTT publication QoS");
    if (result.qos) {
        if (offset + 2 > packet.body.size()) Invalid("Missing MQTT packet identifier");
        result.packet_id = static_cast<std::uint16_t>((packet.body[offset] << 8) | packet.body[offset + 1]);
        if (!result.packet_id) Invalid("Invalid MQTT packet identifier");
        offset += 2;
    }
    result.payload.assign(packet.body.begin() + offset, packet.body.end());
    return result;
}
} // namespace dji_power::cloud
