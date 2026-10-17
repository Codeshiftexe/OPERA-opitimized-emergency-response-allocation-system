#pragma once

// A tiny test harness - three macros and a counter.

#include <cmath>
#include <iostream>

// 'static': each test program is one .cpp file, so each gets its own counters.

static int g_checks = 0;
static int g_fails  = 0;

//multi line macro to check a condition. If it fails, print the file and line 
//number and the condition that failed.
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_fails;                                                           \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond \
                      << '\n';                                                   \
        }                                                                        \
    } while (0)

//multi line macro to check equality. If it fails, print the file and line number
//and the actual and expected values.

#define CHECK_EQ(actual, expected)                                              \
    do {                                                                        \
        ++g_checks;                                                             \
        const auto& checkActual_   = (actual);   /* evaluated exactly ONCE */   \
        const auto& checkExpected_ = (expected);                                \
        if (!(checkActual_ == checkExpected_)) {                                \
            ++g_fails;                                                          \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  "         \
                      << #actual << " == " << #expected                         \
                      << "  (got " << checkActual_                              \
                      << ", want " << checkExpected_ << ")\n";                 \
        }                                                                       \
    } while (0)

//multi line macro to check near equality. If it fails, print the file and line number
//and the actual and expected values.
#define CHECK_NEAR(actual, expected, eps)                                       \
    do {                                                                        \
        ++g_checks;                                                             \
        const double checkActual_   = (actual);  /* evaluated exactly ONCE */   \
        const double checkExpected_ = (expected);                               \
        if (std::fabs(checkActual_ - checkExpected_) > (eps)) {                 \
            ++g_fails;                                                          \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  "         \
                      << #actual << " ~= " << #expected                         \
                      << "  (got " << checkActual_                              \
                      << ", want " << checkExpected_ << ")\n";                 \
        }                                                                       \
    } while (0)

// Call this as the last line of every test's main():
//     return report("test_heap");
inline int report(const char* suiteName) {
    std::cout << suiteName << ": " << (g_checks - g_fails) << "/" << g_checks
              << " checks passed\n";
    return g_fails == 0 ? 0 : 1;
}