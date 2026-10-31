// 本文件验证生成的 DLL 可加载、导出正确且能枚举五个 TrafficMonitor 项目。
#include "PluginInterface.h"
#include <windows.h>
#include "test_check.hpp"
#include <string>

int wmain(int argc, wchar_t** argv) {
    REQUIRE_TEST(argc == 2);
    const auto module = LoadLibraryW(argv[1]);
    REQUIRE_TEST(module != nullptr);
    const auto entry = reinterpret_cast<ITMPlugin*(*)()>(GetProcAddress(module, "TMPluginGetInstance"));
    REQUIRE_TEST(entry != nullptr);
    const auto plugin = entry();
    REQUIRE_TEST(plugin != nullptr && plugin->GetAPIVersion() == 8);
    REQUIRE_TEST(std::wstring(plugin->GetInfo(ITMPlugin::TMI_VERSION)) == L"1.0.0");
    REQUIRE_TEST(std::wstring(plugin->GetInfo(ITMPlugin::TMI_AUTHOR)) == L"DuHeng29");
    REQUIRE_TEST(std::wstring(plugin->GetInfo(ITMPlugin::TMI_COPYRIGHT)) ==
           L"Copyright (c) 2026 DuHeng29");
    REQUIRE_TEST(std::wstring(plugin->GetInfo(ITMPlugin::TMI_URL)) ==
           L"https://github.com/DuHeng29/DJI-Power-for-Traaffic-Monitor");
    for (int index = 0; index < 5; ++index) REQUIRE_TEST(plugin->GetItem(index) != nullptr);
    REQUIRE_TEST(plugin->GetItem(5) == nullptr);
    FreeLibrary(module);
    return 0;
}

