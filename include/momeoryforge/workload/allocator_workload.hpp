#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <filesystem>

#include "memoryforge/common/json.hpp"
#include "memoryforge/common/result.hpp"
#include "memoryforge/workload/allocation_trace.hpp"

namespace memoryforge {

/// Workload families. The kind is selected by `workload_id` (W01..W05).
enum class AllocatorWorkloadKind {
    UniformRandom,     ///< W01: baseline, uniform sizes, random releases.
    MixedLifetimes,    ///< W02: a fraction of long-lived blocks pins holes open.
    BurstAllocation,   ///< W03: allocate in bursts, then release part of the live set.
    SizeSkew,          ///< W04: mostly small requests with occasional large ones.
    LongRunningStress  ///< W05: alternating alloc-heavy/free-heavy phases, mixed sizes and alignments.
};

enum class ReleasePolicy { Uniform, AgeWeighted, Lifo, Fifo };
enum class AlignmentPolicy { Default, PowerOfTwo };

std::optional<AllocatorWorkloadKind> workload_kind_from_id(std::string_view workload_id) noexcept;
std::string_view to_string(ReleasePolicy p) noexcept;
std::string_view to_string(AlignmentPolicy p) noexcept;

/// Effective configuration of one allocator workload (JSON schema version 1).
/// Fields after `warmup_operations` are optional in the file and have defaults.
struct AllocatorWorkloadConfig {
    std::uint64_t schema_version = 1;
    std::string workload_id = "W01";
    std::string name = "unnamed";
    std::uint64_t seed = 1;
    std::uint64_t heap_size_bytes = 1048576;
    std::uint64_t operations = 10000;
    std::uint64_t min_allocation_bytes = 16;
    std::uint64_t max_allocation_bytes = 4096;
    double allocation_probability = 0.6;
    ReleasePolicy release_policy = ReleasePolicy::Uniform;
    AlignmentPolicy alignment_policy = AlignmentPolicy::Default;
    /// Leading operations that are executed but excluded from timing statistics.
    std::uint64_t warmup_operations = 0;

    double target_utilization = 0.75;       ///< Generator frees instead of allocating above this live/heap ratio.
    double long_lived_fraction = 0.2;       ///< W02: share of allocations that are not released voluntarily.
    std::uint64_t burst_size = 200;         ///< W03: allocations per burst.
    double burst_release_fraction = 0.7;    ///< W03: share of the live set released after each burst.
    double large_request_probability = 0.05;///< W04: probability of a large request.
    std::uint64_t repetitions = 5;          ///< Timed repetitions per strategy.
    std::uint64_t sample_interval = 0;      ///< Fragmentation sampling period in ops; 0 = operations / 200.
};

Status validate_config(const AllocatorWorkloadConfig& config);
Result<AllocatorWorkloadConfig> parse_allocator_config(const json::Value& root);
Result<AllocatorWorkloadConfig> load_allocator_config(const std::filesystem::path& path);
void write_config_json(json::Writer& w, const AllocatorWorkloadConfig& config);

/// Generates the allocator-independent trace. Same config => identical trace on every platform.
Result<AllocationTrace> generate_allocation_trace(const AllocatorWorkloadConfig& config);

}  // namespace memoryforge
