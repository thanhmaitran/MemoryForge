#pragma once

// Minimal self-contained test framework (no external dependency).
// Usage: TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
// Run a test binary with an optional substring filter: ./allocator_tests first_fit

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace mftest {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& current_failures() {
    static int count = 0;
    return count;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

template <typename T>
std::string show(const T& v) {
    std::ostringstream os;
    if constexpr (std::is_enum_v<T>) {
        os << static_cast<long long>(v);
    } else if constexpr (requires { os << v; }) {
        os << v;
    } else {
        os << "<unprintable>";
    }
    return os.str();
}

inline void report(const char* file, int line, const std::string& message) {
    ++current_failures();
    std::cerr << "    " << file << ':' << line << ": " << message << '\n';
}

template <typename A, typename B>
void check_eq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
    if (!(a == b)) {
        report(file, line, std::string("CHECK_EQ(") + ea + ", " + eb + ") failed: " + show(a) + " vs " + show(b));
    }
}

}  // namespace mftest

#define MF_CONCAT_INNER(a, b) a##b
#define MF_CONCAT(a, b) MF_CONCAT_INNER(a, b)

#define TEST(name)                                                                  \
    static void name();                                                             \
    static ::mftest::Registrar MF_CONCAT(registrar_, name)(#name, &name);           \
    static void name()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) ::mftest::report(__FILE__, __LINE__, "CHECK(" #cond ") failed"); \
    } while (false)

#define CHECK_EQ(a, b) ::mftest::check_eq((a), (b), #a, #b, __FILE__, __LINE__)
