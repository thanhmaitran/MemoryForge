#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace memoryforge {

/// Runs the command-line interface. `args` excludes the program name.
/// Exit codes: 0 success, 1 runtime or validation failure, 2 usage error.
int run_cli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace memoryforge
