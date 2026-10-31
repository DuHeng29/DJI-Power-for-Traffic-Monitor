// 测试检查在 Release 的 NDEBUG 模式下仍执行，并在失败时返回非零退出码。
#pragma once
#include <cstdlib>
#include <iostream>

inline void RequireTest(bool condition, const char* expression, const char* file, int line) {
    if (condition) return;
    std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    std::exit(EXIT_FAILURE);
}

#define REQUIRE_TEST(...) RequireTest(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __FILE__, __LINE__)
