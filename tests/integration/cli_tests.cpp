#include <filesystem>
#include <fstream>
#include <sstream>

#include "memoryforge/allocator/free_list_allocator.hpp"
#include "memoryforge/benchmark/allocator_benchmark.hpp"
#include "memoryforge/cli/cli.hpp"
#include "memoryforge/common/io.hpp"
#include "memoryforge/common/json.hpp"
#include "memoryforge/vm/virtual_memory.hpp"
#include "memoryforge/workload/page_trace.hpp"
#include "test_framework.hpp"

using namespace memoryforge;
namespace fs = std::filesystem;

#ifndef MEMORYFORGE_SOURCE_DIR
#error "MEMORYFORGE_SOURCE_DIR must be defined by the build system"
#endif

namespace {

struct CliResult {
    int code;
    std::string out;
    std::string err;
};

CliResult cli(std::vector<std::string> args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = run_cli(args, out, err);
    return {code, out.str(), err.str()};
}

std::string source(const std::string& relative) { return (fs::path(MEMORYFORGE_SOURCE_DIR) / relative).string(); }

fs::path scratch_dir() {
    const fs::path dir = fs::temp_directory_path() / "memoryforge_integration_tests";
    fs::create_directories(dir);
    return dir;
}

std::size_t count_lines(const std::string& text) {
    std::size_t n = 0;
    for (const char c : text) n += (c == '\n') ? 1U : 0U;
    return n;
}

}  // namespace

TEST(cli_usage_and_version) {
    CHECK_EQ(cli({}).code, 2);
    CHECK_EQ(cli({"--help"}).code, 0);
    CHECK(cli({"--help"}).out.find("allocator run") != std::string::npos);
    CHECK(cli({"--version"}).out.find("memoryforge 0.") == 0);
    CHECK_EQ(cli({"bogus"}).code, 2);
    CHECK_EQ(cli({"allocator", "run"}).code, 2);                       // missing --config
    CHECK_EQ(cli({"allocator", "run", "--nope", "1"}).code, 2);        // unknown option
    CHECK_EQ(cli({"allocator", "run", "--config"}).code, 2);           // option without value
    CHECK_EQ(cli({"vm", "run", "--config", source("configs/virtual-memory/sequential.json"), "--policy", "mru"}).code, 2);
}

TEST(cli_reports_bad_configs_with_exit_code_1) {
    const fs::path bad = scratch_dir() / "bad.json";
    CHECK(write_text_file(bad, R"({"workload_id": "W01", "oops": 1})").ok());
    const auto r = cli({"allocator", "run", "--config", bad.string()});
    CHECK_EQ(r.code, 1);
    CHECK(r.err.find("unknown field") != std::string::npos);
    CHECK_EQ(cli({"allocator", "run", "--config", (scratch_dir() / "missing.json").string()}).code, 1);
}

TEST(cli_allocator_run_writes_csv_json_and_timeseries) {
    const fs::path dir = scratch_dir();
    const auto r = cli({"allocator", "run", "--config", source("configs/allocator/mixed-lifetimes.json"), "--repetitions", "1",
                        "--validate-every", "500", "--output", (dir / "a.csv").string(), "--timeseries", (dir / "ts.csv").string(),
                        "--json", (dir / "a.json").string()});
    CHECK_EQ(r.code, 0);
    CHECK(r.out.find("drain check passed") != std::string::npos);

    const std::string csv = read_text_file(dir / "a.csv").value();
    CHECK_EQ(count_lines(csv), 4U);  // header + 3 strategies
    CHECK(csv.find("first-fit") != std::string::npos && csv.find("worst-fit") != std::string::npos);

    const std::string ts = read_text_file(dir / "ts.csv").value();
    CHECK(count_lines(ts) > 3 * 100);  // ~200 samples per strategy

    auto report = json::parse(read_text_file(dir / "a.json").value());
    CHECK(report.ok());
    CHECK(report.value().find("environment")->find("compiler_name") != nullptr);
    CHECK_EQ(report.value().find("results")->items().size(), 3U);
    CHECK_EQ(report.value().find("workload_config")->find("seed")->as_uint(), 20261029U);
    CHECK(report.value().find("measurement")->find("trace_checksum_fnv1a64") != nullptr);
}

TEST(cli_allocator_algorithmic_results_are_reproducible) {
    const fs::path dir = scratch_dir();
    const std::string config = source("configs/allocator/burst-allocation.json");
    const auto a = cli({"allocator", "run", "--config", config, "--no-timing", "--output", (dir / "r1.csv").string()});
    const auto b = cli({"allocator", "run", "--config", config, "--no-timing", "--output", (dir / "r2.csv").string()});
    CHECK_EQ(a.code, 0);
    CHECK_EQ(b.code, 0);
    // Without timing, every column is deterministic, so the files must be byte-identical.
    CHECK_EQ(read_text_file(dir / "r1.csv").value(), read_text_file(dir / "r2.csv").value());
}

TEST(cli_trace_export_can_be_replayed) {
    const fs::path dir = scratch_dir();
    const std::string config_path = source("configs/allocator/size-skew.json");
    CHECK_EQ(cli({"allocator", "trace", "--config", config_path, "--output", (dir / "t.txt").string()}).code, 0);
    auto exported = parse_allocation_trace(read_text_file(dir / "t.txt").value());
    CHECK(exported.ok());
    auto config = load_allocator_config(config_path).value();
    CHECK_EQ(exported.value().checksum(), generate_allocation_trace(config).value().checksum());
}

TEST(cli_vm_run_covers_every_policy_and_frame_count) {
    const fs::path dir = scratch_dir();
    const auto r = cli({"vm", "run", "--config", source("configs/virtual-memory/working-set-shift.json"), "--frames", "6,12",
                        "--repetitions", "1", "--validate-every", "100", "--output", (dir / "v.csv").string(), "--json",
                        (dir / "v.json").string()});
    CHECK_EQ(r.code, 0);
    const std::string csv = read_text_file(dir / "v.csv").value();
    CHECK_EQ(count_lines(csv), 1U + 2U * 4U);  // header + 2 frame counts x 4 policies
    CHECK(json::parse(read_text_file(dir / "v.json").value()).ok());
}

TEST(cli_validate_passes_for_shipped_configs_and_builtin_checks) {
    const auto r = cli({"validate", "--all", "--configs", source("configs")});
    CHECK_EQ(r.code, 0);
    CHECK(r.out.find("FAIL") == std::string::npos);
    CHECK(r.out.find("builtin: 13-reference trace, OPT, 3 frames") != std::string::npos);
    CHECK_EQ(cli({"validate"}).code, 2);
}

TEST(replaying_a_fixture_trace_matches_hand_computed_layout) {
    auto trace = parse_allocation_trace(read_text_file(source("tests/fixtures/alloc-trace-small.txt")).value());
    CHECK(trace.ok());
    AllocatorWorkloadConfig config;
    config.heap_size_bytes = 1024;
    AllocatorRunOptions options;
    options.repetitions = 0;
    options.validate_interval = 1;
    options.verify_drain = true;
    for (const AllocationStrategy s : kAllStrategies) {
        auto run = run_allocator_benchmark(config, trace.value(), s, options);
        CHECK(run.ok());
        CHECK(run.value().drain_verified);
        CHECK_EQ(run.value().allocations_failed, 0U);
    }
    // First Fit: 112 | 208 | 304 bytes occupy [0,112) [112,320) [320,624); freeing #1 then asking
    // for 150 bytes (160 reserved) reuses offset 112.
    auto a = make_allocator(AllocationStrategy::FirstFit, 1024).value();
    CHECK(a->allocate(100, 16).ok());
    auto second = a->allocate(200, 16).value();
    CHECK(a->allocate(300, 16).ok());
    CHECK(a->deallocate(second.id).ok());
    CHECK_EQ(a->allocate(150, 16).value().offset, 112U);
}

TEST(classic_fixture_trace_gives_the_textbook_fault_counts) {
    auto trace = parse_page_trace(read_text_file(source("tests/fixtures/classic-trace.txt")).value());
    CHECK(trace.ok());
    CHECK_EQ(trace.value().accesses.size(), 20U);
    const auto vpns = trace.value().vpns();
    const struct {
        ReplacementPolicyKind policy;
        std::uint64_t faults;
    } expected[] = {{ReplacementPolicyKind::Fifo, 15}, {ReplacementPolicyKind::Lru, 12}, {ReplacementPolicyKind::Optimal, 9}};
    for (const auto& e : expected) {
        auto vm = VirtualMemory::create(VmConfig{4096, 16, 3}, make_policy(e.policy, 3, vpns).value()).value();
        for (const Vpn vpn : vpns) CHECK(vm.access(vpn * 4096, AccessType::Read).ok());
        CHECK_EQ(vm.stats().page_faults, e.faults);
    }
}
