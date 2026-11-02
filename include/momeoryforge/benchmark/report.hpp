#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "memoryforge/benchmark/allocator_benchmark.hpp"
#include "memoryforge/benchmark/build_info.hpp"
#include "memoryforge/benchmark/vm_benchmark.hpp"

namespace memoryforge {

/// Everything needed to describe one allocator experiment in a report.
struct AllocatorExperiment {
    AllocatorWorkloadConfig config;  ///< Effective configuration (after CLI overrides).
    AllocatorRunOptions options;
    std::uint64_t trace_operations = 0;
    std::uint64_t trace_checksum = 0;
    std::vector<AllocatorRunResult> runs;
};

struct VmExperiment {
    VmWorkloadConfig config;
    VmRunOptions options;
    std::uint64_t trace_checksum = 0;
    std::vector<VmRunResult> runs;
};

// CSV: one header row, one row per run. Undefined values are written as empty fields.
void write_allocator_summary_csv(std::ostream& out, const BuildInfo& info, const AllocatorExperiment& experiment);
void write_allocator_timeseries_csv(std::ostream& out, const AllocatorExperiment& experiment);
void write_vm_summary_csv(std::ostream& out, const BuildInfo& info, const VmExperiment& experiment);

// JSON: full metadata, effective configuration and every result.
void write_allocator_report_json(std::ostream& out, const BuildInfo& info, const AllocatorExperiment& experiment,
                                 const std::string& generated_at_utc);
void write_vm_report_json(std::ostream& out, const BuildInfo& info, const VmExperiment& experiment,
                          const std::string& generated_at_utc);

}  // namespace memoryforge
