#include <exception>
#include <string>

#include "test_framework.hpp"

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    int failed = 0;
    for (const auto& test : mftest::registry()) {
        if (!filter.empty() && std::string(test.name).find(filter) == std::string::npos) continue;
        ++ran;
        const int before = mftest::current_failures();
        try {
            test.fn();
        } catch (const std::exception& e) {
            mftest::report(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        } catch (...) {
            mftest::report(__FILE__, __LINE__, "unexpected non-standard exception");
        }
        if (mftest::current_failures() != before) {
            ++failed;
            std::cerr << "[FAIL] " << test.name << '\n';
        }
    }
    std::cout << (ran - failed) << '/' << ran << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
