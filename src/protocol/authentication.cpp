#include "protocol/authentication.hpp"
#include "protocol/duml.hpp"
#include <stdexcept>

namespace dji_power::duml {
bool AuthenticatePairKey(const std::string& pair_key, const AuthRequest& request, std::wstring& error) {
    error.clear();
    std::string key;
    try { key = NormalizePairKey(pair_key); }
    catch (const std::invalid_argument&) {
        error = L"Pair Key 必须是 32 个十六进制字符";
        return false;
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::wstring failure;
        const auto challenge = request(L"获取鉴权挑战", {0x00}, failure);
        if (!challenge) { error = L"获取鉴权挑战失败：" + failure; return false; }
        if (challenge->empty()) { error = L"获取鉴权挑战失败：响应为空"; return false; }
        if ((*challenge)[0] != 0) {
            error = L"设备拒绝鉴权挑战（状态码 " + std::to_wstring((*challenge)[0]) + L"）";
            return false;
        }
        if (challenge->size() < 5) { error = L"获取鉴权挑战失败：响应长度不足"; return false; }
        std::vector<std::uint8_t> material{0x01};
        material.insert(material.end(), challenge->begin() + 1, challenge->begin() + 5);
        material.insert(material.end(), key.begin(), key.end());
        material.push_back(0);
        const auto answer = request(L"提交 Pair Key 验证", std::move(material), failure);
        if (!answer) { error = L"提交 Pair Key 验证失败：" + failure; return false; }
        if (answer->empty()) { error = L"Pair Key 验证失败：响应为空"; return false; }
        if ((*answer)[0] == 0) return true;
        // 与参考实现一致：完整的 03 响应允许在同一连接内重取挑战一次。
        // 必须使用新挑战构造验证报文，不能重发旧报文或无限重试。
        if ((*answer)[0] == 3 && answer->size() >= 5 && attempt == 0) continue;
        error = L"设备拒绝 Pair Key 验证（状态码 " + std::to_wstring((*answer)[0]) + L"）";
        return false;
    }
    return false;
}
} // namespace dji_power::duml
