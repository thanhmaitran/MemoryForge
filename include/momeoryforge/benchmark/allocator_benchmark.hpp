#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "memoryforge/allocator/allocator.hpp"
#include "memoryforge/metrics/latency.hpp"
#include "memoryforge/workload/allocation_trace.hpp"
#include "memoryforge/workload/allocator_workload.hpp"

namespace memoryforge {

struct AllocatorRunOptions {
    std::uint64_t repetitions = 5;        ///< Timed repetitions (0 disables wall-clock measurement).
    std::uint64_t warmup_operations = 0;  ///< Leading ops executed but excluded from timing.
    std::uint64_t sample_interval = 0;    ///< Fragmentation sample period in ops (0 = none).
    std::uint64_t validate_interval = 0;  ///< Run allocator invariants every N ops (0 = never, 1 = every op).
    bool verify_drain = false;            ///< After the trace, free everything and check the arena is whole again.
};

struct FragmentationSample {
    std::uint64_t op_index = 0;  ///< Number of operations executed when sampled.
    AllocatorStats stats;
};

/// Results for one (trace, strategy) pair.
///
/// Two kinds of numbers live here and must not be mixed up:
///  * algorithmic results come from one untimed, deterministic pass over the
///    whole trace and are identical on every machine;
///  * wall-clock results come from separate timed repetitions on fresh
///    allocators and depend on the machine, build type and system load.
struct AllocatorRunResult {
    AllocationStrategy strategy = AllocationStrategy::FirstFit;

    // Algorithmic (deterministic).
    std::uint64_t operations = 0;
    std::uint64_t allocate_attempts = 0;
    std::uint64_t allocations_succeeded = 0;
    std::uint64_t allocations_failed = 0;
    std::uint64_t frees_executed = 0;
    std::uint64_t frees_skipped = 0;  ///< Frees of allocations that had failed.
    AllocatorStats final_stats;
    std::vector<FragmentationSample> timeseries;
    std::uint64_t validation_checks = 0;
    bool drain_verified = false;

    // Wall clock (machine dependent).
    std::vector<double> throughput_ops_per_sec;  ///< One value per repetition, warm-up excluded.
    LatencySummary alloc_latency;
    LatencySummary free_latency;
    double timer_overhead_ns = 0.0;

    [[nodiscard]] double failure_rate() const noexcept {
        return allocate_attempts == 0 ? 0.0
                                      : static_cast<double>(allocations_failed) / static_cast<double>(allocate_attempts);
    }
};

/// Replays `trace` against a fresh allocator of the given strategy.
/// Returns an error if the allocator misbehaves (an invariant check fails, a
/// valid free is rejected, an unexpected error code appears).
Result<AllocatorRunResult> run_allocator_benchmark(const AllocatorWorkloadConfig& config,
                                                   const AllocationTrace& trace,
                                                   AllocationStrategy strategy,
                                                   const AllocatorRunOptions& options);

}  // namespace memoryforge
