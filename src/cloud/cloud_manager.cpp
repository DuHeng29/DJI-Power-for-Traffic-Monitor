#include "cloud/cloud_manager.hpp"
#include "cloud/cloud_protocol.hpp"
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Windows.Web.Http.Filters.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
namespace dji_power {
namespace {
namespace foundation = winrt::Windows::Foundation;
namespace streams = winrt::Windows::Storage::Streams;
namespace sockets = winrt::Windows::Networking::Sockets;
namespace http = winrt::Windows::Web::Http;
void CheckStop(std::stop_token token) { if (token.stop_requested()) throw winrt::hresult_canceled(); }
template<typename Operation>
void Wait(const Operation& operation, std::stop_token token, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (operation.Status() == foundation::AsyncStatus::Started) {
        if (token.stop_requested() || std::chrono::steady_clock::now() >= deadline) {
            operation.Cancel();
            CheckStop(token);
            throw std::runtime_error("DJI Cloud network timeout");
        }
        std::this_thread::sleep_for(25ms);
    }
    CheckStop(token);
}
cloud::MqttCredentials FetchCredentials(const PluginConfig& config, std::stop_token token) {
    http::Filters::HttpBaseProtocolFilter filter;
    filter.AllowAutoRedirect(false); // Member Token 只能发送到配置白名单中的 DJI 官方 API。
    filter.CookieUsageBehavior(http::Filters::HttpCookieUsageBehavior::NoCookies);
    filter.CacheControl().ReadBehavior(http::Filters::HttpCacheReadBehavior::NoCache);
    filter.CacheControl().WriteBehavior(http::Filters::HttpCacheWriteBehavior::NoCache);
    http::HttpClient client(filter);
    http::HttpRequestMessage request(http::HttpMethod::Get(), foundation::Uri(
        L"https://" + config.cloud_api_host + L"/app/api/v1/users/auth/token?reason=mqtt"));
    request.Headers().Append(L"x-member-token", winrt::to_hstring(config.cloud_token));
    request.Headers().Append(L"X-DJI-locale", L"en_US");
    request.Headers().Append(L"Cache-Control", L"no-cache, no-store");
    const auto send = client.SendRequestAsync(request);
    Wait(send, token, 30s);
    const auto response = send.GetResults();
    const auto status = static_cast<int>(response.StatusCode());
    if (status == 401 || status == 403) throw cloud::AuthenticationError("DJI Cloud token expired; sign in again");
    if (status != 200) throw std::runtime_error("DJI Cloud credential request failed (HTTP " + std::to_string(status) + ")");
    const auto read = response.Content().ReadAsStringAsync();
    Wait(read, token, 15s);
    const auto text = winrt::to_string(read.GetResults());
    if (text.size() > 1024 * 1024) throw std::runtime_error("DJI Cloud response too large");
    return cloud::ParseCredentials(text);
}
} // namespace

struct CloudManager::Impl {
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::jthread worker;
    bool requested{};
    std::atomic<bool> auto_reconnect{true};
    TelemetryStore store;
    void Stop() {
        if (!worker.joinable()) return;
        worker.request_stop(); condition.notify_all(); worker.join();
    }
    void Session(const PluginConfig& config, std::stop_token token) {
        store.SetStatus(ConnectionState::connecting, L"正在获取 DJI Cloud 连接凭据");
        auto credentials = FetchCredentials(config, token);
        const auto refresh_at = std::chrono::steady_clock::now() + std::chrono::seconds(
            std::max(1, credentials.lifetime_seconds - std::min(900, credentials.lifetime_seconds / 10)));
        sockets::StreamSocket socket;
        struct CloseSocket { sockets::StreamSocket socket; ~CloseSocket() { try { socket.Close(); } catch (...) {} } } close{socket};
        store.SetStatus(ConnectionState::connecting, L"正在连接 DJI Cloud MQTT（TLS）");
        const auto connect = socket.ConnectAsync(winrt::Windows::Networking::HostName(winrt::to_hstring(credentials.domain)),
            winrt::to_hstring(std::to_string(credentials.port)), sockets::SocketProtectionLevel::Tls12);
        Wait(connect, token, 30s); connect.GetResults();
        streams::DataWriter writer(socket.OutputStream());
        streams::DataReader reader(socket.InputStream());
        reader.InputStreamOptions(streams::InputStreamOptions::Partial);
        auto last_sent = std::chrono::steady_clock::now();
        auto last_received = last_sent;
        auto last_telemetry = last_sent;
        const auto send = [&](const cloud::Bytes& bytes) {
            writer.WriteBytes(bytes);
            const auto write = writer.StoreAsync();
            Wait(write, token, 15s);
            if (write.GetResults() != bytes.size()) throw std::runtime_error("MQTT write incomplete");
            last_sent = std::chrono::steady_clock::now();
        };
        send(cloud::ConnectPacket(credentials));
        if (!credentials.password.empty()) SecureZeroMemory(credentials.password.data(), credentials.password.size());
        const auto topic = cloud::PropertyTopic(config.cloud_serial);
        bool connected = false, subscribed = false;
        const auto handshake_deadline = std::chrono::steady_clock::now() + 30s;
        cloud::MqttDecoder decoder;
        auto load = reader.LoadAsync(4096);
        try {
            while (!token.stop_requested()) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= refresh_at) { load.Cancel(); return; }
                if ((!subscribed && now >= handshake_deadline) || now - last_received > 90s)
                    throw std::runtime_error("DJI Cloud MQTT response timeout");
                if (now - last_sent >= 30s) send({0xC0, 0});
                if (subscribed && now - last_telemetry >= 180s && store.Get().connection != ConnectionState::error)
                    store.SetStatus(ConnectionState::error, L"云端设备遥测已超时，请确认电源已联网");
                if (load.Status() == foundation::AsyncStatus::Started) {
                    std::unique_lock lock(mutex);
                    condition.wait_for(lock, 50ms, [&] { return token.stop_requested(); });
                    continue;
                }
                const auto count = load.GetResults();
                if (!count) throw std::runtime_error("DJI Cloud MQTT connection closed");
                cloud::Bytes bytes(count); reader.ReadBytes(bytes); last_received = now;
                for (const auto& packet : decoder.Feed(bytes)) {
                    const auto type = packet.header >> 4;
                    if (type == 2) {
                        if (connected || packet.header != 0x20 || packet.body.size() != 2 || packet.body[0] != 0)
                            throw std::runtime_error("Invalid MQTT CONNACK");
                        if (packet.body[1] != 0) throw std::runtime_error("DJI Cloud MQTT authentication rejected (code " + std::to_string(packet.body[1]) + ")");
                        connected = true; send(cloud::SubscribePacket(topic));
                    } else if (type == 9) {
                        if (!connected || packet.header != 0x90 || packet.body != cloud::Bytes{0, 1, 0})
                            throw std::runtime_error("DJI Cloud telemetry subscription rejected");
                        subscribed = true;
                        store.SetStatus(ConnectionState::connected, L"DJI Cloud 已连接，等待设备遥测");
                    } else if (type == 3) {
                        if (!connected) throw std::runtime_error("MQTT publication before CONNACK");
                        const auto publication = cloud::ParsePublication(packet);
                        if (publication.qos == 1) send({0x40, 2, static_cast<std::uint8_t>(publication.packet_id >> 8),
                            static_cast<std::uint8_t>(publication.packet_id)});
                        if (publication.topic != topic) continue;
                        const auto report = cloud::ParseTelemetry(publication.payload);
                        if (!report) continue;
                        auto value = store.Get();
                        value.battery_percent = report->battery_percent.value_or(-1);
                        value.runtime_min = report->runtime_min.value_or(-1);
                        value.input_w = report->input_w.value_or(-1);
                        value.output_w = report->output_w.value_or(-1);
                        value.device_name = config.device_name;
                        value.connection = ConnectionState::connected;
                        value.status = L"DJI Cloud 已连接";
                        value.stale_after = 180s; value.updated_at = now;
                        last_telemetry = now;
                        store.Update(value);
                    } else if (type == 14) throw std::runtime_error("DJI Cloud MQTT disconnected");
                }
                load = reader.LoadAsync(4096);
            }
        } catch (...) { load.Cancel(); throw; }
        load.Cancel();
    }
    void Run(PluginConfig config, std::stop_token token) {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Uninit { ~Uninit() { winrt::uninit_apartment(); } } uninit;
            while (!token.stop_requested()) {
                {
                    std::unique_lock lock(mutex);
                    condition.wait(lock, [&] { return token.stop_requested() || requested; });
                    requested = false;
                }
                if (token.stop_requested()) break;
                int retry = 1;
                while (!token.stop_requested()) {
                    try { Session(config, token); retry = 1; continue; }
                    catch (const cloud::AuthenticationError&) {
                        store.SetStatus(ConnectionState::error, L"DJI Cloud Token 已失效，请重新登录账号");
                        break; // 长期 Token 失效时等待用户登录，不反复请求账号服务。
                    }
                    catch (const winrt::hresult_canceled&) { if (token.stop_requested()) break; store.SetStatus(ConnectionState::error, L"DJI Cloud 网络操作被取消"); }
                    catch (const winrt::hresult_error& error) {
                        if (token.stop_requested()) break;
                        store.SetStatus(ConnectionState::error, L"DJI Cloud 网络错误（HRESULT " + std::to_wstring(error.code().value) + L"）");
                    } catch (const std::exception& error) {
                        store.SetStatus(ConnectionState::error, winrt::to_hstring(error.what()).c_str());
                    }
                    if (!auto_reconnect) break;
                    std::unique_lock lock(mutex);
                    condition.wait_for(lock, std::chrono::seconds(retry), [&] { return token.stop_requested() || requested; });
                    requested = false; retry = std::min(retry * 2, 30);
                }
            }
        } catch (...) { store.SetStatus(ConnectionState::error, L"DJI Cloud 初始化失败"); }
        if (!config.cloud_token.empty()) SecureZeroMemory(config.cloud_token.data(), config.cloud_token.size());
    }
};
CloudManager& CloudManager::Instance() { static CloudManager manager; return manager; }
CloudManager::CloudManager() : impl_(std::make_unique<Impl>()) {}
CloudManager::~CloudManager() { Stop(); }
void CloudManager::Stop() { impl_->Stop(); }
void CloudManager::Reconfigure(const PluginConfig& config) {
    Stop(); impl_->store.Update(TelemetrySnapshot{});
    std::wstring error;
    if (!cloud::ValidateConfig(config, error)) { impl_->store.SetStatus(ConnectionState::error, error); return; }
    { std::scoped_lock lock(impl_->mutex); impl_->requested = config.auto_connect; }
    impl_->auto_reconnect = config.auto_reconnect;
    impl_->store.SetStatus(ConnectionState::stopped, L"DJI Cloud 等待连接");
    impl_->worker = std::jthread([this, config](std::stop_token token) { impl_->Run(config, token); });
}
void CloudManager::ConnectNow() { std::scoped_lock lock(impl_->mutex); impl_->requested = true; impl_->condition.notify_all(); }
void CloudManager::UpdatePolicy(const PluginConfig& config) { impl_->auto_reconnect = config.auto_reconnect; }
TelemetrySnapshot CloudManager::Snapshot() const { return impl_->store.Get(); }
} // namespace dji_power
