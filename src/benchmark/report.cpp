#include "memoryforge/benchmark/report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>

#include "memoryforge/metrics/fragmentation.hpp"

namespace memoryforge {
namespace {

std::string num(double v) {
    if (!std::isfinite(v)) return "";
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}
std::string num(std::optional<double> v) { return v ? num(*v) : std::string(); }

template <typename T>
std::string integer(T v) {
    return std::to_string(v);
}

std::string field(const std::string& s) {
    if (s.find_first_of(",\"\n\r") == std::string::npos) return s;
    std::string quoted = "\"";
    for (const char c : s) {
        if (c == '"') quoted += '"';
        quoted += c;
    }
    return quoted + "\"";
}

void row(std::ostream& out, const std::vector<std::string>& fields) {
    for (std::size_t i = 0; i < fields.size(); ++i) out << (i ? "," : "") << field(fields[i]);
    out << '\n';
}

double min_of(const std::vector<double>& v) { return v.empty() ? NAN : *std::min_element(v.begin(), v.end()); }
double max_of(const std::vector<double>& v) { return v.empty() ? NAN : *std::max_element(v.begin(), v.end()); }

std::string compiler_label(const BuildInfo& i) { return i.compiler_name + " " + i.compiler_version; }

void write_latency_json(json::Writer& w, const char* name, const LatencySummary& s) {
    w.key(name);
    w.begin_object();
    w.key("count"); w.value(static_cast<std::uint64_t>(s.count));
    w.key("min_ns"); w.value(s.min);
    w.key("mean_ns"); w.value(s.mean);
    w.key("median_ns"); w.value(s.median);
    w.key("p95_ns"); w.value(s.p95);
    w.key("p99_ns"); w.value(s.p99);
    w.key("max_ns"); w.value(s.max);
    w.end_object();
}

void write_optional(json::Writer& w, const char* key, std::optional<double> v) {
    w.key(key);
    if (v) {
        w.value(*v);
    } else {
        w.null();
    }
}

}  // namespace

void write_allocator_summary_csv(std::ostream& out, const BuildInfo& info, const AllocatorExperiment& e) {
    row(out, {"workload_id", "workload_name", "strategy", "seed", "heap_size_bytes", "operations", "warmup_operations",
              "repetitions", "trace_checksum", "allocate_attempts", "allocations_failed", "failure_rate",
              "frees_executed", "frees_skipped", "search_steps", "search_steps_per_attempt",
              "final_live_allocated_bytes", "final_free_bytes", "final_largest_free_block", "final_free_block_count",
              "final_external_fragmentation", "final_internal_overhead_ratio", "peak_live_allocated_bytes",
              "throughput_median_ops_per_s", "throughput_min_ops_per_s", "throughput_max_ops_per_s",
              "alloc_latency_median_ns", "alloc_latency_p95_ns", "alloc_latency_p99_ns", "alloc_latency_mean_ns",
              "alloc_latency_max_ns", "free_latency_median_ns", "free_latency_p95_ns", "free_latency_p99_ns",
              "free_latency_mean_ns", "free_latency_max_ns", "timer_overhead_ns", "build_type", "compiler",
              "git_commit"});
    for (const AllocatorRunResult& r : e.runs) {
        const AllocatorStats& s = r.final_stats;
        const double per_attempt = r.allocate_attempts == 0 ? NAN : static_cast<double>(s.search_steps) / static_cast<double>(r.allocate_attempts);
        row(out, {e.config.workload_id, e.config.name, std::string(to_string(r.strategy)), integer(e.config.seed),
                  integer(e.config.heap_size_bytes), integer(r.operations), integer(e.options.warmup_operations),
                  integer(e.options.repetitions), integer(e.trace_checksum), integer(r.allocate_attempts),
                  integer(r.allocations_failed), num(r.failure_rate()), integer(r.frees_executed),
                  integer(r.frees_skipped), integer(s.search_steps), num(per_attempt), integer(s.live_allocated_bytes),
                  integer(s.free_bytes), integer(s.largest_free_block), integer(s.free_block_count),
                  num(external_fragmentation_indicator(s)), num(internal_overhead_ratio(s)),
                  integer(s.peak_live_allocated_bytes), num(median_of(r.throughput_ops_per_sec)),
                  num(min_of(r.throughput_ops_per_sec)), num(max_of(r.throughput_ops_per_sec)),
                  num(r.alloc_latency.median), num(r.alloc_latency.p95), num(r.alloc_latency.p99),
                  num(r.alloc_latency.mean), num(r.alloc_latency.max), num(r.free_latency.median),
                  num(r.free_latency.p95), num(r.free_latency.p99), num(r.free_latency.mean), num(r.free_latency.max),
                  num(r.timer_overhead_ns), info.build_type, compiler_label(info), info.git_commit});
    }
}

void write_allocator_timeseries_csv(std::ostream& out, const AllocatorExperiment& e) {
    row(out, {"workload_id", "strategy", "op_index", "live_allocated_bytes", "free_bytes", "largest_free_block",
              "free_block_count", "external_fragmentation", "failed_allocations"});
    for (const AllocatorRunResult& r : e.runs) {
        for (const FragmentationSample& sample : r.timeseries) {
            const AllocatorStats& s = sample.stats;
            row(out, {e.config.workload_id, std::string(to_string(r.strategy)), integer(sample.op_index),
                      integer(s.live_allocated_bytes), integer(s.free_bytes), integer(s.largest_free_block),
                      integer(s.free_block_count), num(external_fragmentation_indicator(s)),
                      integer(s.failed_allocations)});
        }
    }
}

void write_vm_summary_csv(std::ostream& out, const BuildInfo& info, const VmExperiment& e) {
    row(out, {"workload_id", "workload_name", "pattern", "seed", "page_size", "virtual_pages", "frames", "policy",
              "trace_checksum", "accesses", "hits", "page_faults", "fault_rate", "compulsory_faults", "replacements",
              "writebacks", "metadata_bytes", "accesses_per_s_median", "accesses_per_s_min", "accesses_per_s_max",
              "build_type", "compiler", "git_commit"});
    for (const VmRunResult& r : e.runs) {
        row(out, {e.config.workload_id, e.config.name, std::string(to_string(e.config.pattern)), integer(e.config.seed),
                  integer(e.config.page_size), integer(e.config.virtual_pages), integer(r.frames),
                  std::string(to_string(r.policy)), integer(e.trace_checksum), integer(r.stats.accesses),
                  integer(r.stats.hits), integer(r.stats.page_faults), num(r.stats.fault_rate()),
                  integer(r.stats.compulsory_faults), integer(r.stats.replacements), integer(r.stats.writebacks),
                  integer(r.metadata_bytes), num(median_of(r.accesses_per_sec)), num(min_of(r.accesses_per_sec)),
                  num(max_of(r.accesses_per_sec)), info.build_type, compiler_label(info), info.git_commit});
    }
}

void write_allocator_report_json(std::ostream& out, const BuildInfo& info, const AllocatorExperiment& e,
                                 const std::string& generated_at_utc) {
    json::Writer w(out);
    w.begin_object();
    w.key("report_type"); w.value("allocator_experiment");
    w.key("schema_version"); w.value(1);
    w.key("generated_at_utc"); w.value(generated_at_utc);
    w.key("environment"); write_build_info_json(w, info);
    w.key("workload_config"); write_config_json(w, e.config);
    w.key("measurement");
    w.begin_object();
    w.key("repetitions"); w.value(e.options.repetitions);
    w.key("warmup_operations"); w.value(e.options.warmup_operations);
    w.key("sample_interval"); w.value(e.options.sample_interval);
    w.key("validate_interval"); w.value(e.options.validate_interval);
    w.key("latency_unit"); w.value("ns");
    w.key("throughput_unit"); w.value("operations_per_second");
    w.key("trace_operations"); w.value(e.trace_operations);
    w.key("trace_checksum_fnv1a64"); w.value(e.trace_checksum);
    w.end_object();
    w.key("results");
    w.begin_array();
    for (const AllocatorRunResult& r : e.runs) {
        const AllocatorStats& s = r.final_stats;
        w.begin_object();
        w.key("strategy"); w.value(to_string(r.strategy));
        w.key("algorithmic");
        w.begin_object();
        w.key("allocate_attempts"); w.value(r.allocate_attempts);
        w.key("allocations_succeeded"); w.value(r.allocations_succeeded);
        w.key("allocations_failed"); w.value(r.allocations_failed);
        w.key("failure_rate"); w.value(r.failure_rate());
        w.key("frees_executed"); w.value(r.frees_executed);
        w.key("frees_skipped_after_failed_allocation"); w.value(r.frees_skipped);
        w.key("search_steps"); w.value(s.search_steps);
        w.key("final_live_allocation_count"); w.value(static_cast<std::uint64_t>(s.live_allocation_count));
        w.key("final_live_requested_bytes"); w.value(static_cast<std::uint64_t>(s.live_requested_bytes));
        w.key("final_live_allocated_bytes"); w.value(static_cast<std::uint64_t>(s.live_allocated_bytes));
        w.key("final_free_bytes"); w.value(static_cast<std::uint64_t>(s.free_bytes));
        w.key("final_largest_free_block"); w.value(static_cast<std::uint64_t>(s.largest_free_block));
        w.key("final_free_block_count"); w.value(static_cast<std::uint64_t>(s.free_block_count));
        write_optional(w, "final_external_fragmentation", external_fragmentation_indicator(s));
        write_optional(w, "final_internal_overhead_ratio", internal_overhead_ratio(s));
        w.key("peak_live_allocated_bytes"); w.value(static_cast<std::uint64_t>(s.peak_live_allocated_bytes));
        w.key("validation_checks"); w.value(r.validation_checks);
        w.key("drain_verified"); w.value(r.drain_verified);
        w.end_object();
        w.key("wall_clock");
        w.begin_object();
        w.key("timer_overhead_ns"); w.value(r.timer_overhead_ns);
        w.key("throughput_ops_per_s_by_repetition");
        w.begin_array();
        for (const double t : r.throughput_ops_per_sec) w.value(t);
        w.end_array();
        w.key("throughput_median_ops_per_s"); w.value(median_of(r.throughput_ops_per_sec));
        write_latency_json(w, "allocate_latency", r.alloc_latency);
        write_latency_json(w, "free_latency", r.free_latency);
        w.end_object();
        w.end_object();
    }
    w.end_array();
    w.end_object();
    out << '\n';
}

void write_vm_report_json(std::ostream& out, const BuildInfo& info, const VmExperiment& e,
                          const std::string& generated_at_utc) {
    json::Writer w(out);
    w.begin_object();
    w.key("report_type"); w.value("virtual_memory_experiment");
    w.key("schema_version"); w.value(1);
    w.key("generated_at_utc"); w.value(generated_at_utc);
    w.key("environment"); write_build_info_json(w, info);
    w.key("workload_config"); write_config_json(w, e.config);
    w.key("measurement");
    w.begin_object();
    w.key("repetitions"); w.value(e.options.repetitions);
    w.key("validate_interval"); w.value(e.options.validate_interval);
    w.key("throughput_unit"); w.value("accesses_per_second");
    w.key("trace_checksum_fnv1a64"); w.value(e.trace_checksum);
    w.end_object();
    w.key("results");
    w.begin_array();
    for (const VmRunResult& r : e.runs) {
        w.begin_object();
        w.key("policy"); w.value(to_string(r.policy));
        w.key("frames"); w.value(r.frames);
        w.key("algorithmic");
        w.begin_object();
        w.key("accesses"); w.value(r.stats.accesses);
        w.key("reads"); w.value(r.stats.reads);
        w.key("writes"); w.value(r.stats.writes);
        w.key("hits"); w.value(r.stats.hits);
        w.key("page_faults"); w.value(r.stats.page_faults);
        w.key("fault_rate"); w.value(r.stats.fault_rate());
        w.key("compulsory_faults"); w.value(r.stats.compulsory_faults);
        w.key("replacements"); w.value(r.stats.replacements);
        w.key("writebacks"); w.value(r.stats.writebacks);
        w.key("metadata_bytes"); w.value(r.metadata_bytes);
        w.key("validation_checks"); w.value(r.validation_checks);
        w.end_object();
        w.key("wall_clock");
        w.begin_object();
        w.key("accesses_per_s_by_repetition");
        w.begin_array();
        for (const double t : r.accesses_per_sec) w.value(t);
        w.end_array();
        w.key("accesses_per_s_median"); w.value(median_of(r.accesses_per_sec));
        w.end_object();
        w.end_object();
    }
    w.end_array();
    w.end_object();
    out << '\n';
}

}  // namespace memoryforge
