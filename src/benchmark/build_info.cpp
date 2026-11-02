#include "memoryforge/benchmark/build_info.hpp"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>

#ifndef MEMORYFORGE_VERSION
#define MEMORYFORGE_VERSION "0.1.0"
#endif
#ifndef MEMORYFORGE_GIT_COMMIT
#define MEMORYFORGE_GIT_COMMIT "unknown"
#endif
#ifndef MEMORYFORGE_BUILD_TYPE
#define MEMORYFORGE_BUILD_TYPE "unknown"
#endif

namespace memoryforge {
namespace {

std::string cpu_description() {
#if defined(__linux__)
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos && colon + 2 <= line.size()) return line.substr(colon + 2);
        }
    }
#elif defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "PROCESSOR_IDENTIFIER") == 0 && value != nullptr) {
        std::string result(value);
        std::free(value);
        return result;
    }
#endif
    return "unavailable";
}

}  // namespace

BuildInfo collect_build_info() {
    BuildInfo info;
    info.project_version = MEMORYFORGE_VERSION;
    info.git_commit = MEMORYFORGE_GIT_COMMIT;
    info.build_type = MEMORYFORGE_BUILD_TYPE;
#if defined(__clang__)
    info.compiler_name = "Clang";
    info.compiler_version = std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__) + "." +
                            std::to_string(__clang_patchlevel__);
#elif defined(__GNUC__)
    info.compiler_name = "GCC";
    info.compiler_version = std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." +
                            std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    info.compiler_name = "MSVC";
    info.compiler_version = std::to_string(_MSC_FULL_VER);
#else
    info.compiler_name = "unknown";
    info.compiler_version = "unknown";
#endif
#if defined(_WIN32)
    info.operating_system = "Windows";
#elif defined(__linux__)
    info.operating_system = "Linux";
#elif defined(__APPLE__)
    info.operating_system = "macOS";
#else
    info.operating_system = "unknown";
#endif
    info.cpu_description = cpu_description();
    info.hardware_threads = std::thread::hardware_concurrency();
    return info;
}

std::string utc_timestamp_now() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

void write_build_info_json(json::Writer& w, const BuildInfo& i) {
    w.begin_object();
    w.key("project_version"); w.value(i.project_version);
    w.key("git_commit"); w.value(i.git_commit);
    w.key("compiler_name"); w.value(i.compiler_name);
    w.key("compiler_version"); w.value(i.compiler_version);
    w.key("build_type"); w.value(i.build_type);
    w.key("operating_system"); w.value(i.operating_system);
    w.key("cpu_description"); w.value(i.cpu_description);
    w.key("hardware_threads"); w.value(i.hardware_threads);
    w.end_object();
}

}  // namespace memoryforge
