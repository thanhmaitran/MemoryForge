#pragma once

#include <string>

#include "memoryforge/common/json.hpp"

namespace memoryforge {

/// Provenance recorded with every report so a result can be traced to the
/// exact build and machine that produced it.
struct BuildInfo {
    std::string project_version;
    std::string git_commit;  ///< Captured at CMake configure time; "unknown" outside a git checkout.
    std::string compiler_name;
    std::string compiler_version;
    std::string build_type;
    std::string operating_system;
    std::string cpu_description;  ///< "unavailable" when the platform gives no cheap answer.
    unsigned hardware_threads = 0;
};

BuildInfo collect_build_info();
std::string utc_timestamp_now();
void write_build_info_json(json::Writer& w, const BuildInfo& info);

}  // namespace memoryforge
