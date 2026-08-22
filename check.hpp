// Minimal test harness. The library layer takes no dependencies; neither does
// its test suite. Enough to name the failing check and its line, no more.
#pragma once

#include <iostream>
#include <string>

namespace pztest {

inline int g_checks = 0;
inline int g_failures = 0;

inline void report(bool ok, const char* expr, const char* file, int line,
                   const std::string& extra = {}) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::cout << "FAIL " << file << ':' << line << "  " << expr;
    if (!extra.empty()) std::cout << "\n     " << extra;
    std::cout << '\n';
}

inline int summary() {
    std::cout << (g_failures == 0 ? "PASS" : "FAIL") << "  " << g_checks
              << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}

} // namespace pztest

#define CHECK(expr) ::pztest::report((expr), #expr, __FILE__, __LINE__)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        const auto _a = (a);                                                   \
        const auto _b = (b);                                                   \
        ::pztest::report(_a == _b, #a " == " #b, __FILE__, __LINE__,            \
                         "got " + ::pztest::show(_a) + ", want " + ::pztest::show(_b)); \
    } while (0)

#define CHECK_THROWS(...)                                                      \
    do {                                                                       \
        bool _threw = false;                                                   \
        try { (void)(__VA_ARGS__); } catch (const ::pzformat::ParseError&) { _threw = true; } \
        ::pztest::report(_threw, "throws: " #__VA_ARGS__, __FILE__, __LINE__);  \
    } while (0)

namespace pztest {

inline std::string show(const std::string& s) { return '"' + s + '"'; }
inline std::string show(bool b) { return b ? "true" : "false"; }
template <typename T>
inline std::string show(T v) { return std::to_string(v); }

} // namespace pztest
