// 可离线验证的 DJI Power 挑战鉴权流程；传输由 BLE 层提供。
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dji_power::duml {
using AuthRequest = std::function<std::optional<std::vector<std::uint8_t>>(
    std::wstring_view stage, std::vector<std::uint8_t> payload, std::wstring& failure)>;
bool AuthenticatePairKey(const std::string& pair_key, const AuthRequest& request, std::wstring& error);
} // namespace dji_power::duml
