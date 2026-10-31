// 使用合成数据离线验证 Cloud 字段、MQTT 分包及非法输入；不连接任何网络。
#include "cloud/cloud_protocol.hpp"
#include "test_check.hpp"
#include <winrt/base.h>
#include <stdexcept>

using namespace dji_power::cloud;
template<typename Function>
bool Rejects(Function function) {
    try { function(); } catch (const std::runtime_error&) { return true; }
    return false;
}
int main() {
    winrt::init_apartment();
    dji_power::PluginConfig config;
    config.cloud_token = "CN_test-token";
    config.cloud_serial = L"TEST123";
    std::wstring error;
    REQUIRE_TEST(ValidateConfig(config, error));
    config.cloud_token += "\r\nHeader: injected";
    REQUIRE_TEST(!ValidateConfig(config, error));
    config.cloud_token = "US_test-token";
    config.cloud_serial = L"+/#";
    REQUIRE_TEST(!ValidateConfig(config, error));
    config.cloud_serial = L"TEST123";
    config.cloud_api_host = L"example.com";
    REQUIRE_TEST(!ValidateConfig(config, error));

    const auto credentials = ParseCredentials(R"({"result":{"code":0},"data":{
        "mqtt_domain":"mqtt-test.djigate.com","mqtt_port":"8883","client_id":"client",
        "user_uuid":"user","user_token":"secret","expire":14400}})");
    REQUIRE_TEST(credentials.port == 8883 && credentials.lifetime_seconds == 14400);
    REQUIRE_TEST(credentials.domain == "mqtt-test.djigate.com" && credentials.username == "user");
    REQUIRE_TEST(Rejects([] { ParseCredentials(R"({"data":{"mqtt_domain":"djigate.com.example.net"}})"); }));
    REQUIRE_TEST(Rejects([] { ParseCredentials(R"({"result":{"code":121010}})"); }));
    REQUIRE_TEST(Rejects([] { ParseCredentials(R"({"data":{"mqtt_domain":"bad/path"}})"); }));
    REQUIRE_TEST(Rejects([] { ParseCredentials("not JSON"); }));
    const auto report = ParseTelemetry(R"({"method":"device_osd","data":{"host":{
        "battery":{"charge_pct":7850,"remain_time":"222"},
        "power_info":{"input":436,"output":127}}}})");
    REQUIRE_TEST(report && report->battery_percent == 78 && report->runtime_min == 222);
    REQUIRE_TEST(report->input_w == 436 && report->output_w == 127);
    const auto partial = ParseTelemetry(R"({"method":"device_osd","data":{"host":{"power_info":{"input":0}}}})");
    REQUIRE_TEST(partial && partial->input_w == 0 && !partial->output_w && !partial->battery_percent);
    REQUIRE_TEST(!ParseTelemetry(R"({"method":"device_state","data":{"host":{"battery":{"charge_pct":8000}}}})"));
    REQUIRE_TEST(!ParseTelemetry(R"({"method":"device_osd","data":{"host":{"battery":{"charge_pct":10001},"power_info":{"input":-1}}}})"));
    REQUIRE_TEST(!ParseTelemetry("[]"));
    const auto topic = PropertyTopic(L"TEST123");
    REQUIRE_TEST(topic == "forward/dy/thing/product/TEST123/property");
    REQUIRE_TEST(Rejects([] { PropertyTopic(L"bad/#"); }));

    MqttDecoder connect_decoder;
    const auto connect = connect_decoder.Feed(ConnectPacket(credentials));
    REQUIRE_TEST(connect.size() == 1 && connect[0].header == 0x10);
    const Bytes connect_prefix{0,4,'M','Q','T','T',4,0xC2,0,60};
    REQUIRE_TEST(std::equal(connect_prefix.begin(), connect_prefix.end(), connect[0].body.begin()));
    MqttDecoder subscribe_decoder;
    const auto subscribe = subscribe_decoder.Feed(SubscribePacket(topic));
    REQUIRE_TEST(subscribe.size() == 1 && subscribe[0].header == 0x82 && subscribe[0].body[1] == 1);
    REQUIRE_TEST(subscribe[0].body.back() == 0);
    // 两字节 Remaining Length，逐字节分片，随后合并 PINGRESP。
    Bytes publication{0x30, 0x85, 0x01, 0, 1, 't'};
    publication.insert(publication.end(), 130, 'x');
    MqttDecoder decoder;
    std::vector<MqttPacket> decoded;
    for (const auto byte : publication) {
        const Bytes chunk{byte};
        const auto packets = decoder.Feed(chunk);
        decoded.insert(decoded.end(), packets.begin(), packets.end());
    }
    REQUIRE_TEST(decoded.size() == 1);
    const auto message = ParsePublication(decoded[0]);
    REQUIRE_TEST(message.topic == "t" && message.payload == std::string(130, 'x'));
    const Bytes combined{0xD0,0,0x20,2,0,0};
    REQUIRE_TEST(decoder.Feed(combined).size() == 2);
    const auto qos1 = ParsePublication({0x32, {0,1,'t',0,7,'v'}});
    REQUIRE_TEST(qos1.qos == 1 && qos1.packet_id == 7 && qos1.payload == "v");
    REQUIRE_TEST(Rejects([] { ParsePublication({0x32, {0,1,'t'}}); }));
    REQUIRE_TEST(Rejects([] { MqttDecoder invalid; const Bytes bytes{0x30,0xFF,0xFF,0xFF,0xFF}; invalid.Feed(bytes); }));
    winrt::uninit_apartment();
    return 0;
}
