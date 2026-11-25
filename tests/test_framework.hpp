#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace luma::test {

struct TestCase {
    const char* name;
    void (*fn)(int& failures);
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)(int&)) { registry().push_back({name, fn}); }
};

inline int runAll() {
    int passed = 0;
    int failed = 0;
    for (const auto& tc : registry()) {
        int local_failures = 0;
        tc.fn(local_failures);
        if (local_failures == 0) {
            std::cout << "[  PASSED  ] " << tc.name << "\n";
            ++passed;
        } else {
            std::cerr << "[  FAILED  ] " << tc.name << " (" << local_failures
                      << " assertion(s) failed)\n";
            ++failed;
        }
    }
    std::cout << "Summary: " << passed << " passed, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace luma::test

#define LUMA_TEST(name)                                                                     \
    static void luma_test_##name(int& luma_failures);                                         \
    static ::luma::test::Registrar luma_reg_##name(#name, &luma_test_##name);              \
    static void luma_test_##name(int& luma_failures)

#define LUMA_EXPECT_TRUE(cond)                                                              \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::cerr << "  " << __FILE__ << ":" << __LINE__                              \
                      << " EXPECT_TRUE failed: " #cond "\n";                              \
            ++luma_failures;                                                                \
        }                                                                                 \
    } while (0)

#define LUMA_EXPECT_FALSE(cond) LUMA_EXPECT_TRUE(!(cond))

#define LUMA_EXPECT_EQ(actual, expected)                                                    \
    do {                                                                                  \
        const auto& luma_a = (actual);                                                      \
        const auto& luma_e = (expected);                                                    \
        if (!(luma_a == luma_e)) {                                                            \
            std::cerr << "  " << __FILE__ << ":" << __LINE__                              \
                      << " EXPECT_EQ failed: " #actual " != " #expected "\n";             \
            ++luma_failures;                                                                \
        }                                                                                 \
    } while (0)

#define LUMA_TEST_MAIN()                                                                    \
    int main() { return ::luma::test::runAll(); }
