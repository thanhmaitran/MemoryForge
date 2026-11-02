#pragma once

#include <cstdint>
#include <vector>

#include "memoryforge/vm/virtual_memory.hpp"
#include "memoryforge/workload/page_trace.hpp"

namespace memoryforge {

struct VmRunOptions {
    std::uint64_t repetitions = 5;        ///< Timed repetitions (0 disables wall-clock measurement).
    std::uint64_t validate_interval = 0;  ///< Check VM invariants every N accesses (0 = never).
};

/// Results for one (trace, policy, frame count) combination. Fault counts are
/// deterministic; accesses_per_sec depends on the machine.
struct VmRunResult {
    ReplacementPolicyKind policy = ReplacementPolicyKind::Fifo;
    std::uint64_t frames = 0;
    VmStats stats;
    std::uint64_t metadata_bytes = 0;
    std::uint64_t validation_checks = 0;
    std::vector<double> accesses_per_sec;  ///< One value per repetition.
};

Result<VmRunResult> run_vm_benchmark(const VmWorkloadConfig& config, const PageTrace& trace,
                                     ReplacementPolicyKind policy, std::uint64_t frames,
                                     const VmRunOptions& options);

}  // namespace memoryforge
