// 无需蓝牙硬件，模拟设备回复并检查实际发送的鉴权载荷。
#include "protocol/authentication.hpp"
#include "test_check.hpp"

using Bytes = std::vector<std::uint8_t>;
using Reply = std::optional<Bytes>;
constexpr const char* kKey = "ABCDEF0123456789ABCDEF0123456789";

bool Run(const std::string& key, const std::vector<Reply>& replies,
         std::vector<Bytes>& sent, std::wstring& error) {
    sent.clear();
    return dji_power::duml::AuthenticatePairKey(key,
        [&](std::wstring_view, Bytes payload, std::wstring& failure) -> Reply {
            const auto index = sent.size();
            REQUIRE_TEST(index < replies.size());
            sent.push_back(std::move(payload));
            if (!replies[index]) failure = L"等待设备响应超时";
            return replies[index];
        }, error);
}

int main() {
    const Bytes first_challenge{0, 0x11, 0x22, 0x33, 0x44};
    const Bytes new_challenge{0, 0x55, 0x66, 0x77, 0x88};
    const Bytes rejected{3, 0, 0, 0, 0};
    std::vector<Bytes> sent;
    std::wstring error;
    REQUIRE_TEST(Run(kKey, {first_challenge, Bytes{0}}, sent, error));
    REQUIRE_TEST(error.empty() && sent.size() == 2);
    // Key 必须以 32 字节小写 ASCII 发送，而不是解码成 16 字节二进制。
    const std::string first_wire = std::string("\x01\x11\x22\x33\x44", 5) +
        "abcdef0123456789abcdef0123456789" + '\0';
    REQUIRE_TEST(sent[0] == Bytes{0});
    REQUIRE_TEST(sent[1] == Bytes(first_wire.begin(), first_wire.end()));

    REQUIRE_TEST(Run(kKey, {first_challenge, rejected, new_challenge, Bytes{0}}, sent, error));
    REQUIRE_TEST(sent.size() == 4 && sent[2] == Bytes{0});
    const std::string new_wire = std::string("\x01\x55\x66\x77\x88", 5) +
        "abcdef0123456789abcdef0123456789" + '\0';
    REQUIRE_TEST(sent[3] == Bytes(new_wire.begin(), new_wire.end()));

    REQUIRE_TEST(!Run(kKey, {first_challenge, rejected, new_challenge, rejected}, sent, error));
    REQUIRE_TEST(sent.size() == 4 && error.find(L"3") != std::wstring::npos);
    REQUIRE_TEST(!Run(kKey, {first_challenge, Bytes{3}}, sent, error));
    REQUIRE_TEST(sent.size() == 2); // 残缺的 03 响应不触发重试。
    REQUIRE_TEST(!Run(kKey, {first_challenge, Bytes{2, 0, 0, 0, 0}}, sent, error));
    REQUIRE_TEST(sent.size() == 2 && error.find(L"2") != std::wstring::npos);
    REQUIRE_TEST(!Run(kKey, {Bytes{0, 1, 2}}, sent, error));
    REQUIRE_TEST(sent.size() == 1 && !error.empty());
    REQUIRE_TEST(!Run(kKey, {first_challenge, std::nullopt}, sent, error));
    REQUIRE_TEST(sent.size() == 2 && error.find(L"等待设备响应超时") != std::wstring::npos);
    REQUIRE_TEST(!Run(kKey, {first_challenge, Bytes{}}, sent, error));
    REQUIRE_TEST(sent.size() == 2 && !error.empty());
    REQUIRE_TEST(!Run("invalid", {}, sent, error));
    REQUIRE_TEST(sent.empty() && !error.empty());
    return 0;
}
