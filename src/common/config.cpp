// 本文件实现 INI 配置读写；pair_key 只以当前用户可解密的 DPAPI 密文落盘。
#include "common/config.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <vector>

extern HMODULE g_module;

namespace dji_power {
namespace {
std::wstring ConfigPath() { return ModuleDirectory() + L"\\config.ini"; }
std::wstring ReadIni(const wchar_t* key, const wchar_t* fallback = L"") {
    std::vector<wchar_t> buffer(2048);
    const auto path = ConfigPath();
    for (;;) {
        const auto length = GetPrivateProfileStringW(L"connection", key, fallback, buffer.data(),
            static_cast<DWORD>(buffer.size()), path.c_str());
        if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
        // 密文随 Key 数量增长；检测截断后扩大缓冲区，不能解密残缺密文。
        buffer.resize(buffer.size() * 2);
    }
}
std::wstring Protect(const std::string& plain) {
    if (plain.empty()) return {};
    DATA_BLOB input{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"DJI Power pair key", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    DWORD chars = 0;
    CryptBinaryToStringW(output.pbData, output.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars);
    std::wstring encoded(chars, L'\0');
    CryptBinaryToStringW(output.pbData, output.cbData, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &chars);
    LocalFree(output.pbData);
    if (!encoded.empty() && encoded.back() == L'\0') encoded.pop_back();
    return encoded;
}
std::string Unprotect(const std::wstring& encoded) {
    if (encoded.empty()) return {};
    DWORD bytes = 0;
    if (!CryptStringToBinaryW(encoded.c_str(), 0, CRYPT_STRING_BASE64, nullptr, &bytes, nullptr, nullptr)) return {};
    std::vector<BYTE> encrypted(bytes);
    if (!CryptStringToBinaryW(encoded.c_str(), 0, CRYPT_STRING_BASE64, encrypted.data(), &bytes, nullptr, nullptr)) return {};
    DATA_BLOB input{bytes, encrypted.data()}, output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    std::string plain(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return plain;
}
template<typename T>
void AppendUnique(std::vector<T>& values, const T& value) {
    if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}
// Token 长度不固定，使用四字节长度前缀保存整个历史列表，再整体 DPAPI 加密。
std::string PackTokens(const std::vector<std::string>& values) {
    std::string result;
    for (const auto& value : values) {
        const auto length = static_cast<std::uint32_t>(value.size());
        for (int shift = 0; shift < 32; shift += 8) result.push_back(static_cast<char>(length >> shift));
        result += value;
    }
    return result;
}
std::vector<std::string> UnpackTokens(const std::string& bytes) {
    std::vector<std::string> values;
    for (std::size_t offset = 0; offset < bytes.size();) {
        if (bytes.size() - offset < 4) return {};
        std::uint32_t length = 0;
        for (int shift = 0; shift < 32; shift += 8)
            length |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset++])) << shift;
        if (length > bytes.size() - offset) return {};
        AppendUnique(values, bytes.substr(offset, length));
        offset += length;
    }
    return values;
}
} // namespace

std::wstring ModuleDirectory() {
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(g_module, path.data(), static_cast<DWORD>(path.size()));
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path().wstring();
}
bool IsValidPairKey(const std::string& value) {
    return value.size() == 32 && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}
bool SameConnectionSettings(const PluginConfig& left, const PluginConfig& right) {
    return left.auto_connect == right.auto_connect && left.auto_reconnect == right.auto_reconnect && SameConnectionTarget(left, right);
}
bool SameConnectionTarget(const PluginConfig& left, const PluginConfig& right) {
    if (left.mode != right.mode) return false;
    if (left.mode == ConnectionMode::cloud)
        return left.cloud_serial == right.cloud_serial && left.cloud_token == right.cloud_token && left.cloud_api_host == right.cloud_api_host;
    return left.bluetooth_address == right.bluetooth_address && left.pair_key.size() == right.pair_key.size() &&
        std::equal(left.pair_key.begin(), left.pair_key.end(), right.pair_key.begin(), [](unsigned char a, unsigned char b) {
            return std::tolower(a) == std::tolower(b);
        });
}
PluginConfig LoadConfig() {
    PluginConfig result;
    result.mode = ReadIni(L"mode") == L"cloud" ? ConnectionMode::cloud : ConnectionMode::ble;
    result.cloud_token = Unprotect(ReadIni(L"cloud_token_dpapi"));
    result.cloud_serial = ReadIni(L"cloud_serial");
    result.cloud_tokens = UnpackTokens(Unprotect(ReadIni(L"cloud_tokens_dpapi")));
    AppendUnique(result.cloud_tokens, result.cloud_token);
    const auto serials = ReadIni(L"cloud_serials");
    for (std::size_t offset = 0; offset < serials.size();) {
        const auto end = serials.find(L';', offset);
        AppendUnique(result.cloud_serials, serials.substr(offset, end == std::wstring::npos ? end : end - offset));
        if (end == std::wstring::npos) break;
        offset = end + 1;
    }
    AppendUnique(result.cloud_serials, result.cloud_serial);
    result.cloud_api_host = ReadIni(L"cloud_api_host", L"home-api.djigate.com");
    result.device_name = ReadIni(L"device_name");
    result.auto_connect = ReadIni(L"auto_connect", L"1") != L"0";
    result.auto_reconnect = ReadIni(L"auto_reconnect", L"1") != L"0";
    try { result.bluetooth_address = std::stoull(ReadIni(L"bluetooth_address", L"0"), nullptr, 16); } catch (...) { result.bluetooth_address = 0; }
    result.pair_key = Unprotect(ReadIni(L"pair_key_dpapi"));
    if (!IsValidPairKey(result.pair_key)) result.pair_key.clear();
    const auto keys = Unprotect(ReadIni(L"pair_keys_dpapi"));
    for (std::size_t offset = 0; offset + 32 <= keys.size(); offset += 32) {
        const auto key = keys.substr(offset, 32);
        if (IsValidPairKey(key) && std::find(result.pair_keys.begin(), result.pair_keys.end(), key) == result.pair_keys.end())
            result.pair_keys.push_back(key);
    }
    if (!result.pair_key.empty() && std::find(result.pair_keys.begin(), result.pair_keys.end(), result.pair_key) == result.pair_keys.end())
        result.pair_keys.push_back(result.pair_key);
    return result;
}
bool SaveConfig(const PluginConfig& config) {
    const auto path = ConfigPath();
    wchar_t address[17]{};
    swprintf_s(address, L"%012llX", static_cast<unsigned long long>(config.bluetooth_address));
    const auto secret = Protect(config.pair_key);
    const auto cloud_secret = Protect(config.cloud_token);
    if (!config.cloud_token.empty() && cloud_secret.empty()) return false;
    auto cloud_tokens = config.cloud_tokens;
    AppendUnique(cloud_tokens, config.cloud_token);
    const auto token_bytes = PackTokens(cloud_tokens);
    const auto cloud_secrets = Protect(token_bytes);
    if (!token_bytes.empty() && cloud_secrets.empty()) return false;
    auto cloud_serials = config.cloud_serials;
    AppendUnique(cloud_serials, config.cloud_serial);
    std::wstring serial_list;
    for (const auto& serial : cloud_serials) serial_list += serial + L";";
    std::string keys;
    for (const auto& key : config.pair_keys) if (IsValidPairKey(key)) keys += key;
    const auto secrets = Protect(keys);
    if ((!config.pair_key.empty() && secret.empty()) || (!keys.empty() && secrets.empty())) return false;
    return WritePrivateProfileStringW(L"connection", L"mode", config.mode == ConnectionMode::cloud ? L"cloud" : L"ble", path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"cloud_token_dpapi", cloud_secret.c_str(), path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"cloud_tokens_dpapi", cloud_secrets.c_str(), path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"cloud_serial", config.cloud_serial.c_str(), path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"cloud_serials", serial_list.c_str(), path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"cloud_api_host", config.cloud_api_host.c_str(), path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"device_name", config.device_name.c_str(), path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"bluetooth_address", address, path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"pair_key_dpapi", secret.c_str(), path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"pair_keys_dpapi", secrets.c_str(), path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"auto_connect", config.auto_connect ? L"1" : L"0", path.c_str()) &&
           WritePrivateProfileStringW(L"connection", L"auto_reconnect", config.auto_reconnect ? L"1" : L"0", path.c_str());
}
} // namespace dji_power

