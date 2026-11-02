#include "memoryforge/cli/cli.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <set>
#include <sstream>

#include "memoryforge/benchmark/allocator_benchmark.hpp"
#include "memoryforge/benchmark/build_info.hpp"
#include "memoryforge/benchmark/report.hpp"
#include "memoryforge/benchmark/vm_benchmark.hpp"
#include "memoryforge/common/io.hpp"
#include "memoryforge/metrics/fragmentation.hpp"
#include "memoryforge/workload/page_trace.hpp"

namespace memoryforge {
namespace {

namespace fs = std::filesystem;

constexpr int kOk = 0;
constexpr int kFailure = 1;
constexpr int kUsage = 2;

struct Io {
    std::ostream& out;
    std::ostream& err;
};

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    std::set<std::string> flags;

    [[nodiscard]] const std::string* get(const std::string& key) const {
        const auto it = options.find(key);
        return it == options.end() ? nullptr : &it->second;
    }
    [[nodiscard]] bool flag(const std::string& key) const { return flags.count(key) != 0; }
};

const std::set<std::string> kBooleanFlags = {"validate", "all", "help", "version", "no-timing"};

Result<Args> parse_args(const std::vector<std::string>& argv) {
    Args args;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        const std::string& token = argv[i];
        if (token.rfind("--", 0) != 0) {
            args.positional.push_back(token);
            continue;
        }
        std::string key = token.substr(2);
        std::optional<std::string> inline_value;
        if (const auto eq = key.find('='); eq != std::string::npos) {
            inline_value = key.substr(eq + 1);
            key.resize(eq);
        }
        if (kBooleanFlags.count(key) != 0) {
            args.flags.insert(key);
        } else if (inline_value) {
            args.options[key] = *inline_value;
        } else if (i + 1 < argv.size()) {
            args.options[key] = argv[++i];
        } else {
            return make_error(ErrorCode::InvalidArgument, "option --" + key + " requires a value");
        }
    }
    return args;
}

Status check_options(const Args& a, std::initializer_list<const char*> options, std::initializer_list<const char*> flags) {
    for (const auto& [key, value] : a.options) {
        static_cast<void>(value);
        if (std::none_of(options.begin(), options.end(), [&](const char* o) { return key == o; })) {
            return make_error(ErrorCode::InvalidArgument, "unknown option --" + key + " for this command");
        }
    }
    for (const std::string& key : a.flags) {
        if (std::none_of(flags.begin(), flags.end(), [&](const char* f) { return key == f; })) {
            return make_error(ErrorCode::InvalidArgument, "unknown flag --" + key + " for this command");
        }
    }
    return Status::success();
}

Result<std::uint64_t> option_u64(const Args& a, const std::string& key, std::uint64_t fallback) {
    const std::string* text = a.get(key);
    if (text == nullptr) return fallback;
    std::uint64_t value = 0;
    const auto* end = text->data() + text->size();
    const auto r = std::from_chars(text->data(), end, value);
    if (text->empty() || r.ec != std::errc{} || r.ptr != end) {
        return make_error(ErrorCode::InvalidArgument, "--" + key + " expects a non-negative integer, got '" + *text + "'");
    }
    return value;
}

std::vector<std::string> split_list(const std::string& text) {
    std::vector<std::string> parts;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) parts.push_back(item);
    }
    return parts;
}

Result<std::vector<AllocationStrategy>> parse_strategy_list(const std::string& text) {
    std::vector<AllocationStrategy> list;
    for (const std::string& name : split_list(text)) {
        if (name == "all") return std::vector<AllocationStrategy>(std::begin(kAllStrategies), std::end(kAllStrategies));
        const auto s = parse_strategy(name);
        if (!s) return make_error(ErrorCode::InvalidArgument, "unknown strategy '" + name + "' (use first-fit, best-fit, worst-fit or all)");
        list.push_back(*s);
    }
    if (list.empty()) return make_error(ErrorCode::InvalidArgument, "no strategy given");
    return list;
}

Result<std::vector<ReplacementPolicyKind>> parse_policy_list(const std::string& text) {
    std::vector<ReplacementPolicyKind> list;
    for (const std::string& name : split_list(text)) {
        if (name == "all") return std::vector<ReplacementPolicyKind>(std::begin(kAllPolicies), std::end(kAllPolicies));
        const auto p = parse_policy(name);
        if (!p) return make_error(ErrorCode::InvalidArgument, "unknown policy '" + name + "' (use fifo, lru, opt, clock or all)");
        list.push_back(*p);
    }
    if (list.empty()) return make_error(ErrorCode::InvalidArgument, "no policy given");
    return list;
}

Result<std::vector<std::uint64_t>> parse_u64_list(const std::string& text, const std::string& what) {
    std::vector<std::uint64_t> values;
    for (const std::string& item : split_list(text)) {
        std::uint64_t v = 0;
        const auto* end = item.data() + item.size();
        const auto r = std::from_chars(item.data(), end, v);
        if (r.ec != std::errc{} || r.ptr != end || v == 0) {
            return make_error(ErrorCode::InvalidArgument, what + " expects positive integers separated by commas, got '" + item + "'");
        }
        values.push_back(v);
    }
    if (values.empty()) return make_error(ErrorCode::InvalidArgument, what + " is empty");
    return values;
}

int fail(Io& io, const Error& e, int code = kFailure) {
    io.err << "error: " << e.message << '\n';
    return code;
}

Status write_file(Io& io, const std::string& path, const std::string& content) {
    Status s = write_text_file(path, content);
    if (s) io.out << "wrote " << path << '\n';
    return s;
}

std::string fixed(double v, int precision) {
    if (!std::isfinite(v)) return "n/a";
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.*f", precision, v);
    return buf;
}

void print_environment(Io& io, const BuildInfo& info) {
    io.out << "environment: " << info.compiler_name << ' ' << info.compiler_version << ", " << info.build_type << " build, "
           << info.operating_system << ", " << info.cpu_description << ", git " << info.git_commit << '\n';
    if (info.build_type != "Release" && info.build_type != "RelWithDebInfo" && info.build_type != "MinSizeRel") {
        io.out << "note: this is not an optimised build; timing columns are not comparable with Release results\n";
    }
}

std::string usage_text() {
    return R"(MemoryForge - Virtual Memory & Memory Allocator Laboratory

Usage:
  memoryforge allocator run   --config FILE [--strategy first-fit|best-fit|worst-fit|all]
                              [--repetitions N] [--warmup N] [--no-timing]
                              [--validate | --validate-every N]
                              [--output SUMMARY.csv] [--timeseries SERIES.csv] [--json REPORT.json]
  memoryforge allocator trace --config FILE --output TRACE.txt
  memoryforge vm run          --config FILE [--policy fifo|lru|opt|clock|all] [--frames 4,8,16]
                              [--repetitions N] [--no-timing]
                              [--validate | --validate-every N]
                              [--output SUMMARY.csv] [--json REPORT.json]
  memoryforge vm trace        --config FILE --output TRACE.txt
  memoryforge validate        (--all [--configs DIR] | --config FILE)
  memoryforge info
  memoryforge --version | --help

Exit codes: 0 success, 1 failure (including a failed validation), 2 usage error.
)";
}

// ---------------------------------------------------------------- allocator --

void print_allocator_table(Io& io, const AllocatorExperiment& e) {
    io.out << "strategy    attempts   failed  fail%    frag%  free_blks  largest_free  steps/alloc  alloc_p50ns  alloc_p99ns  Mops/s\n";
    for (const AllocatorRunResult& r : e.runs) {
        const AllocatorStats& s = r.final_stats;
        const auto frag = external_fragmentation_indicator(s);
        const double steps = r.allocate_attempts == 0 ? 0.0 : static_cast<double>(s.search_steps) / static_cast<double>(r.allocate_attempts);
        const double mops = median_of(r.throughput_ops_per_sec) / 1e6;
        char line[256];
        std::snprintf(line, sizeof line, "%-10s %9llu %8llu %6s %8s %10zu %13zu %12s %12s %12s %7s\n",
                      std::string(to_string(r.strategy)).c_str(), static_cast<unsigned long long>(r.allocate_attempts),
                      static_cast<unsigned long long>(r.allocations_failed), fixed(r.failure_rate() * 100.0, 2).c_str(),
                      frag ? fixed(*frag * 100.0, 1).c_str() : "n/a", s.free_block_count, s.largest_free_block,
                      fixed(steps, 1).c_str(), fixed(r.alloc_latency.median, 0).c_str(), fixed(r.alloc_latency.p99, 0).c_str(),
                      fixed(mops, 2).c_str());
        io.out << line;
    }
}

int cmd_allocator_run(const Args& a, Io& io) {
    if (Status s = check_options(a, {"config", "strategy", "repetitions", "warmup", "output", "timeseries", "json", "validate-every"},
                                 {"validate", "no-timing"});
        !s) {
        return fail(io, s.error(), kUsage);
    }
    const std::string* config_path = a.get("config");
    if (config_path == nullptr) return fail(io, make_error(ErrorCode::InvalidArgument, "--config is required"), kUsage);

    auto config = load_allocator_config(*config_path);
    if (!config) return fail(io, config.error());
    AllocatorWorkloadConfig cfg = std::move(config).value();

    auto reps = option_u64(a, "repetitions", cfg.repetitions);
    auto warmup = option_u64(a, "warmup", cfg.warmup_operations);
    auto every = option_u64(a, "validate-every", a.flag("validate") ? 1 : 0);
    for (const auto* r : {&reps, &warmup, &every}) {
        if (!*r) return fail(io, r->error(), kUsage);
    }
    cfg.repetitions = std::max<std::uint64_t>(reps.value(), 1);
    cfg.warmup_operations = warmup.value();
    if (Status s = validate_config(cfg); !s) return fail(io, s.error());

    auto strategies = parse_strategy_list(a.get("strategy") != nullptr ? *a.get("strategy") : "all");
    if (!strategies) return fail(io, strategies.error(), kUsage);

    auto trace = generate_allocation_trace(cfg);
    if (!trace) return fail(io, trace.error());

    AllocatorExperiment experiment;
    experiment.config = cfg;
    experiment.trace_operations = trace.value().ops.size();
    experiment.trace_checksum = trace.value().checksum();
    experiment.options.repetitions = a.flag("no-timing") ? 0 : cfg.repetitions;
    experiment.options.warmup_operations = cfg.warmup_operations;
    experiment.options.validate_interval = every.value();
    experiment.options.verify_drain = true;
    if (a.get("timeseries") != nullptr) {
        experiment.options.sample_interval = cfg.sample_interval != 0 ? cfg.sample_interval : std::max<std::uint64_t>(1, cfg.operations / 200);
    }

    const BuildInfo info = collect_build_info();
    io.out << "workload " << cfg.workload_id << " (" << cfg.name << "): " << cfg.operations << " operations, heap "
           << cfg.heap_size_bytes << " bytes, seed " << cfg.seed << ", trace checksum " << experiment.trace_checksum << '\n';
    print_environment(io, info);

    for (const AllocationStrategy strategy : strategies.value()) {
        auto run = run_allocator_benchmark(cfg, trace.value(), strategy, experiment.options);
        if (!run) return fail(io, run.error());
        experiment.runs.push_back(std::move(run).value());
    }
    print_allocator_table(io, experiment);
    if (every.value() != 0) io.out << "invariants validated (" << experiment.runs.front().validation_checks << " checks per strategy); drain check passed\n";

    if (const std::string* path = a.get("output")) {
        std::ostringstream csv;
        write_allocator_summary_csv(csv, info, experiment);
        if (Status s = write_file(io, *path, csv.str()); !s) return fail(io, s.error());
    }
    if (const std::string* path = a.get("timeseries")) {
        std::ostringstream csv;
        write_allocator_timeseries_csv(csv, experiment);
        if (Status s = write_file(io, *path, csv.str()); !s) return fail(io, s.error());
    }
    if (const std::string* path = a.get("json")) {
        std::ostringstream json;
        write_allocator_report_json(json, info, experiment, utc_timestamp_now());
        if (Status s = write_file(io, *path, json.str()); !s) return fail(io, s.error());
    }
    return kOk;
}

int cmd_allocator_trace(const Args& a, Io& io) {
    if (Status s = check_options(a, {"config", "output"}, {}); !s) return fail(io, s.error(), kUsage);
    const std::string* config_path = a.get("config");
    const std::string* output = a.get("output");
    if (config_path == nullptr || output == nullptr) return fail(io, make_error(ErrorCode::InvalidArgument, "--config and --output are required"), kUsage);
    auto config = load_allocator_config(*config_path);
    if (!config) return fail(io, config.error());
    auto trace = generate_allocation_trace(config.value());
    if (!trace) return fail(io, trace.error());
    if (Status s = write_file(io, *output, format_allocation_trace(trace.value())); !s) return fail(io, s.error());
    io.out << trace.value().ops.size() << " operations, checksum " << trace.value().checksum() << '\n';
    return kOk;
}

// ------------------------------------------------------------------- vm -----

void print_vm_table(Io& io, const VmExperiment& e) {
    io.out << "policy  frames   accesses     faults  fault%   compulsory  replacements  writebacks  Maccess/s\n";
    for (const VmRunResult& r : e.runs) {
        char line[200];
        std::snprintf(line, sizeof line, "%-6s %7llu %10llu %10llu %7s %12llu %13llu %11llu %10s\n",
                      std::string(to_string(r.policy)).c_str(), static_cast<unsigned long long>(r.frames),
                      static_cast<unsigned long long>(r.stats.accesses), static_cast<unsigned long long>(r.stats.page_faults),
                      fixed(r.stats.fault_rate() * 100.0, 2).c_str(), static_cast<unsigned long long>(r.stats.compulsory_faults),
                      static_cast<unsigned long long>(r.stats.replacements), static_cast<unsigned long long>(r.stats.writebacks),
                      fixed(median_of(r.accesses_per_sec) / 1e6, 2).c_str());
        io.out << line;
    }
}

int cmd_vm_run(const Args& a, Io& io) {
    if (Status s = check_options(a, {"config", "policy", "frames", "repetitions", "output", "json", "validate-every"}, {"validate", "no-timing"}); !s) {
        return fail(io, s.error(), kUsage);
    }
    const std::string* config_path = a.get("config");
    if (config_path == nullptr) return fail(io, make_error(ErrorCode::InvalidArgument, "--config is required"), kUsage);
    auto config = load_vm_config(*config_path);
    if (!config) return fail(io, config.error());
    VmWorkloadConfig cfg = std::move(config).value();

    auto reps = option_u64(a, "repetitions", cfg.repetitions);
    auto every = option_u64(a, "validate-every", a.flag("validate") ? 1 : 0);
    for (const auto* r : {&reps, &every}) {
        if (!*r) return fail(io, r->error(), kUsage);
    }
    cfg.repetitions = std::max<std::uint64_t>(reps.value(), 1);
    if (const std::string* frames = a.get("frames")) {
        auto list = parse_u64_list(*frames, "--frames");
        if (!list) return fail(io, list.error(), kUsage);
        cfg.frames = std::move(list).value();
    }
    if (Status s = validate_config(cfg); !s) return fail(io, s.error());
    auto policies = parse_policy_list(a.get("policy") != nullptr ? *a.get("policy") : "all");
    if (!policies) return fail(io, policies.error(), kUsage);

    auto trace = generate_page_trace(cfg);
    if (!trace) return fail(io, trace.error());

    VmExperiment experiment;
    experiment.config = cfg;
    experiment.trace_checksum = trace.value().checksum();
    experiment.options.repetitions = a.flag("no-timing") ? 0 : cfg.repetitions;
    experiment.options.validate_interval = every.value();

    const BuildInfo info = collect_build_info();
    io.out << "workload " << cfg.workload_id << " (" << cfg.name << ", " << to_string(cfg.pattern) << "): " << cfg.accesses
           << " accesses, " << cfg.virtual_pages << " virtual pages of " << cfg.page_size << " bytes, seed " << cfg.seed
           << ", trace checksum " << experiment.trace_checksum << '\n';
    print_environment(io, info);

    for (const std::uint64_t frames : cfg.frames) {
        for (const ReplacementPolicyKind policy : policies.value()) {
            auto run = run_vm_benchmark(cfg, trace.value(), policy, frames, experiment.options);
            if (!run) return fail(io, run.error());
            experiment.runs.push_back(std::move(run).value());
        }
    }
    print_vm_table(io, experiment);
    if (every.value() != 0) io.out << "invariants validated every " << every.value() << " access(es)\n";

    if (const std::string* path = a.get("output")) {
        std::ostringstream csv;
        write_vm_summary_csv(csv, info, experiment);
        if (Status s = write_file(io, *path, csv.str()); !s) return fail(io, s.error());
    }
    if (const std::string* path = a.get("json")) {
        std::ostringstream json;
        write_vm_report_json(json, info, experiment, utc_timestamp_now());
        if (Status s = write_file(io, *path, json.str()); !s) return fail(io, s.error());
    }
    return kOk;
}

int cmd_vm_trace(const Args& a, Io& io) {
    if (Status s = check_options(a, {"config", "output"}, {}); !s) return fail(io, s.error(), kUsage);
    const std::string* config_path = a.get("config");
    const std::string* output = a.get("output");
    if (config_path == nullptr || output == nullptr) return fail(io, make_error(ErrorCode::InvalidArgument, "--config and --output are required"), kUsage);
    auto config = load_vm_config(*config_path);
    if (!config) return fail(io, config.error());
    auto trace = generate_page_trace(config.value());
    if (!trace) return fail(io, trace.error());
    if (Status s = write_file(io, *output, format_page_trace(trace.value())); !s) return fail(io, s.error());
    io.out << trace.value().accesses.size() << " accesses, checksum " << trace.value().checksum() << '\n';
    return kOk;
}

// ------------------------------------------------------------- validate -----

struct Check {
    std::string name;
    bool passed;
    std::string detail;
};

std::uint64_t adaptive_interval(std::uint64_t operations) { return std::max<std::uint64_t>(1, operations / 1000); }

Check validate_allocator_file(const fs::path& path) {
    auto config = load_allocator_config(path);
    if (!config) return {path.generic_string(), false, config.error().message};
    auto first = generate_allocation_trace(config.value());
    auto second = generate_allocation_trace(config.value());
    if (!first || !second) return {path.generic_string(), false, "trace generation failed"};
    if (first.value().checksum() != second.value().checksum()) {
        return {path.generic_string(), false, "the same seed produced two different traces"};
    }
    AllocatorRunOptions options;
    options.repetitions = 0;
    options.validate_interval = adaptive_interval(first.value().ops.size());
    options.verify_drain = true;
    std::uint64_t checks = 0;
    for (const AllocationStrategy strategy : kAllStrategies) {
        auto run = run_allocator_benchmark(config.value(), first.value(), strategy, options);
        if (!run) return {path.generic_string(), false, run.error().message};
        checks += run.value().validation_checks;
    }
    return {path.generic_string(), true, "3 strategies, " + std::to_string(checks) + " invariant checks, drain verified, trace reproducible"};
}

Check validate_vm_file(const fs::path& path) {
    auto config = load_vm_config(path);
    if (!config) return {path.generic_string(), false, config.error().message};
    auto first = generate_page_trace(config.value());
    auto second = generate_page_trace(config.value());
    if (!first || !second) return {path.generic_string(), false, "trace generation failed"};
    if (first.value().checksum() != second.value().checksum()) {
        return {path.generic_string(), false, "the same seed produced two different traces"};
    }
    VmRunOptions options;
    options.repetitions = 0;
    options.validate_interval = adaptive_interval(first.value().accesses.size());
    std::uint64_t checks = 0;
    for (const std::uint64_t frames : config.value().frames) {
        std::map<ReplacementPolicyKind, std::uint64_t> faults;
        for (const ReplacementPolicyKind policy : kAllPolicies) {
            auto run = run_vm_benchmark(config.value(), first.value(), policy, frames, options);
            if (!run) return {path.generic_string(), false, run.error().message};
            faults[policy] = run.value().stats.page_faults;
            checks += run.value().validation_checks;
        }
        for (const auto& [policy, count] : faults) {
            if (faults[ReplacementPolicyKind::Optimal] > count) {
                return {path.generic_string(), false,
                        "OPT produced more faults than " + std::string(to_string(policy)) + " with " + std::to_string(frames) + " frames"};
            }
        }
    }
    return {path.generic_string(), true, std::to_string(config.value().frames.size()) + " frame counts x 4 policies, " +
                                             std::to_string(checks) + " invariant checks, OPT lower bound holds"};
}

/// Hand-verified reference results that do not depend on any config file.
std::vector<Check> builtin_checks() {
    std::vector<Check> checks;
    auto fault_count = [](const char* trace_text, ReplacementPolicyKind policy, std::uint64_t frames) -> std::optional<std::uint64_t> {
        auto trace = parse_page_trace(trace_text);
        if (!trace) return std::nullopt;
        VmWorkloadConfig config;
        config.virtual_pages = 64;
        config.frames = {frames};
        VmRunOptions options;
        options.repetitions = 0;
        options.validate_interval = 1;
        auto run = run_vm_benchmark(config, trace.value(), policy, frames, options);
        if (!run) return std::nullopt;
        return run.value().stats.page_faults;
    };
    struct Expectation {
        const char* name;
        const char* trace;
        ReplacementPolicyKind policy;
        std::uint64_t frames;
        std::uint64_t faults;
    };
    const char* const textbook = "7 0 1 2 0 3 0 4 2 3 0 3 2";
    const char* const textbook_full = "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1";
    const char* const belady = "1 2 3 4 1 2 5 1 2 3 4 5";
    const Expectation expectations[] = {
        {"13-reference trace, FIFO, 3 frames", textbook, ReplacementPolicyKind::Fifo, 3, 10},
        {"13-reference trace, LRU, 3 frames", textbook, ReplacementPolicyKind::Lru, 3, 9},
        {"13-reference trace, OPT, 3 frames", textbook, ReplacementPolicyKind::Optimal, 3, 7},
        {"20-reference trace, FIFO, 3 frames", textbook_full, ReplacementPolicyKind::Fifo, 3, 15},
        {"20-reference trace, LRU, 3 frames", textbook_full, ReplacementPolicyKind::Lru, 3, 12},
        {"20-reference trace, OPT, 3 frames", textbook_full, ReplacementPolicyKind::Optimal, 3, 9},
        {"Belady anomaly, FIFO, 3 frames", belady, ReplacementPolicyKind::Fifo, 3, 9},
        {"Belady anomaly, FIFO, 4 frames", belady, ReplacementPolicyKind::Fifo, 4, 10},
    };
    for (const Expectation& e : expectations) {
        const auto got = fault_count(e.trace, e.policy, e.frames);
        const bool ok = got && *got == e.faults;
        checks.push_back({std::string("builtin: ") + e.name, ok,
                          ok ? std::to_string(e.faults) + " page faults, as expected"
                             : "expected " + std::to_string(e.faults) + " page faults, got " + (got ? std::to_string(*got) : "an error")});
    }
    return checks;
}

int cmd_validate(const Args& a, Io& io) {
    if (Status s = check_options(a, {"config", "configs"}, {"all"}); !s) return fail(io, s.error(), kUsage);
    std::vector<Check> results;
    const std::string* single = a.get("config");
    if (single != nullptr) {
        auto text = read_text_file(*single);
        if (!text) return fail(io, text.error());
        auto doc = json::parse(text.value());
        if (!doc) return fail(io, doc.error());
        results.push_back(doc.value().find("pattern") != nullptr ? validate_vm_file(*single) : validate_allocator_file(*single));
    } else if (a.flag("all")) {
        const fs::path root = a.get("configs") != nullptr ? fs::path(*a.get("configs")) : fs::path("configs");
        for (const char* group : {"allocator", "virtual-memory"}) {
            const fs::path dir = root / group;
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) continue;
            std::vector<fs::path> files;
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.path().extension() == ".json") files.push_back(entry.path());
            }
            std::sort(files.begin(), files.end());
            for (const fs::path& file : files) {
                results.push_back(std::string(group) == "allocator" ? validate_allocator_file(file) : validate_vm_file(file));
            }
        }
        if (results.empty()) {
            return fail(io, make_error(ErrorCode::IoError, "no configuration files found under '" + root.generic_string() + "'"));
        }
        for (Check& c : builtin_checks()) results.push_back(std::move(c));
    } else {
        return fail(io, make_error(ErrorCode::InvalidArgument, "validate needs --all or --config FILE"), kUsage);
    }

    std::size_t failures = 0;
    for (const Check& c : results) {
        io.out << (c.passed ? "PASS  " : "FAIL  ") << c.name << "  -  " << c.detail << '\n';
        if (!c.passed) ++failures;
    }
    io.out << (results.size() - failures) << " passed, " << failures << " failed\n";
    return failures == 0 ? kOk : kFailure;
}

int cmd_info(Io& io) {
    const BuildInfo info = collect_build_info();
    json::Writer w(io.out);
    write_build_info_json(w, info);
    io.out << '\n';
    return kOk;
}

}  // namespace

int run_cli(const std::vector<std::string>& argv, std::ostream& out, std::ostream& err) {
    Io io{out, err};
    auto parsed = parse_args(argv);
    if (!parsed) return fail(io, parsed.error(), kUsage);
    const Args& args = parsed.value();

    if (args.flag("version")) {
        out << "memoryforge " << collect_build_info().project_version << '\n';
        return kOk;
    }
    if (args.flag("help") || args.positional.empty() || args.positional[0] == "help") {
        out << usage_text();
        return args.positional.empty() && !args.flag("help") ? kUsage : kOk;
    }

    const std::string& command = args.positional[0];
    const std::string sub = args.positional.size() > 1 ? args.positional[1] : "";
    if (args.positional.size() > 2) return fail(io, make_error(ErrorCode::InvalidArgument, "unexpected argument '" + args.positional[2] + "'"), kUsage);

    if (command == "allocator" && sub == "run") return cmd_allocator_run(args, io);
    if (command == "allocator" && sub == "trace") return cmd_allocator_trace(args, io);
    if (command == "vm" && sub == "run") return cmd_vm_run(args, io);
    if (command == "vm" && sub == "trace") return cmd_vm_trace(args, io);
    if (command == "validate" && sub.empty()) return cmd_validate(args, io);
    if (command == "info" && sub.empty()) return cmd_info(io);
    if (command == "version" && sub.empty()) {
        out << "memoryforge " << collect_build_info().project_version << '\n';
        return kOk;
    }
    err << "error: unknown command '" << command << (sub.empty() ? "" : " " + sub) << "'\n\n" << usage_text();
    return kUsage;
}

}  // namespace memoryforge
