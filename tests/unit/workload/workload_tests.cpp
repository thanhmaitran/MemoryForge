#include <algorithm>
#include <set>

#include "memoryforge/allocator/free_list_allocator.hpp"
#include "memoryforge/common/json.hpp"
#include "memoryforge/common/rng.hpp"
#include "memoryforge/workload/allocator_workload.hpp"
#include "memoryforge/workload/page_trace.hpp"
#include "test_framework.hpp"

using namespace memoryforge;

namespace {
AllocatorWorkloadConfig config_for(const char* id) {
    AllocatorWorkloadConfig c;
    c.workload_id = id;
    c.operations = 5000;
    c.seed = 42;
    return c;
}
}  // namespace

TEST(rng_is_deterministic_and_unbiased_in_range) {
    Rng a(123);
    Rng b(123);
    Rng c(124);
    bool differs = false;
    for (int i = 0; i < 100; ++i) {
        const auto x = a.next_u64();
        CHECK_EQ(x, b.next_u64());
        differs = differs || (x != c.next_u64());
    }
    CHECK(differs);
    Rng r(5);
    std::set<std::uint64_t> seen;
    for (int i = 0; i < 2000; ++i) {
        const auto v = r.uniform(3, 9);
        CHECK(v >= 3 && v <= 9);
        seen.insert(v);
    }
    CHECK_EQ(seen.size(), 7U);
    for (int i = 0; i < 1000; ++i) {
        const double d = r.uniform01();
        CHECK(d >= 0.0 && d < 1.0);
    }
}

TEST(rng_golden_values_guard_cross_platform_reproducibility) {
    // If these change, every seeded workload (and every committed report) changes with them.
    Rng r(1);
    CHECK_EQ(r.next_u64(), 0xB3F2AF6D0FC710C5ULL);
    CHECK_EQ(r.next_u64(), 0x853B559647364CEAULL);
}

TEST(same_config_gives_identical_traces_for_every_workload) {
    for (const char* id : {"W01", "W02", "W03", "W04", "W05"}) {
        auto c = config_for(id);
        auto t1 = generate_allocation_trace(c).value();
        auto t2 = generate_allocation_trace(c).value();
        CHECK_EQ(t1.checksum(), t2.checksum());
        CHECK_EQ(t1.ops.size(), c.operations);
        CHECK(validate_trace(t1).ok());
        c.seed = 43;
        CHECK(generate_allocation_trace(c).value().checksum() != t1.checksum());
    }
}

TEST(trace_respects_size_alignment_and_live_cap) {
    for (const char* id : {"W01", "W02", "W03", "W04", "W05"}) {
        auto c = config_for(id);
        c.alignment_policy = AlignmentPolicy::PowerOfTwo;
        auto trace = generate_allocation_trace(c).value();
        std::vector<std::uint64_t> sizes;
        std::uint64_t live = 0;
        std::uint64_t peak = 0;
        for (const AllocOp& op : trace.ops) {
            if (op.kind == AllocOp::Kind::Allocate) {
                CHECK(op.size >= c.min_allocation_bytes && op.size <= c.max_allocation_bytes);
                CHECK(op.alignment >= 16 && op.alignment <= 256 && (op.alignment & (op.alignment - 1)) == 0);
                const std::uint64_t rounded = (op.size + 15) / 16 * 16;
                sizes.push_back(rounded);
                live += rounded;
                peak = std::max(peak, live);
            } else {
                live -= sizes[static_cast<std::size_t>(op.ordinal)];
            }
        }
        CHECK(static_cast<double>(peak) <= c.target_utilization * static_cast<double>(c.heap_size_bytes) + static_cast<double>(c.max_allocation_bytes) + 16.0);
    }
}

TEST(workloads_have_their_characteristic_shape) {
    auto skew = config_for("W04");
    skew.max_allocation_bytes = 65536;
    skew.large_request_probability = 0.05;
    auto t = generate_allocation_trace(skew).value();
    std::size_t large = 0;
    std::size_t total = 0;
    for (const AllocOp& op : t.ops) {
        if (op.kind != AllocOp::Kind::Allocate) continue;
        ++total;
        if (op.size >= skew.max_allocation_bytes / 2) ++large;
    }
    CHECK(large > 0 && large < total / 4);

    auto burst = config_for("W03");
    burst.burst_size = 50;
    auto b = generate_allocation_trace(burst).value();
    std::size_t longest_run = 0;
    std::size_t run = 0;
    for (const AllocOp& op : b.ops) {
        run = (op.kind == AllocOp::Kind::Allocate) ? run + 1 : 0;
        longest_run = std::max(longest_run, run);
    }
    CHECK(longest_run >= 20);  // allocations arrive in bursts

    auto uniform = config_for("W01");
    auto u = generate_allocation_trace(uniform).value();
    std::size_t u_run = 0;
    std::size_t u_longest = 0;
    for (const AllocOp& op : u.ops) {
        u_run = (op.kind == AllocOp::Kind::Allocate) ? u_run + 1 : 0;
        u_longest = std::max(u_longest, u_run);
    }
    CHECK(u_longest < longest_run);
}

TEST(generator_granularity_matches_allocator) {
    CHECK_EQ(FreeListAllocator::kGranularity, 16U);  // the generator hard-codes 16 when sizing live bytes
}

TEST(config_parsing_accepts_the_documented_example) {
    const char* text = R"({
      "schema_version": 1, "workload_id": "W02", "name": "mixed-lifetimes", "seed": 20261029,
      "heap_size_bytes": 1048576, "operations": 50000, "min_allocation_bytes": 16,
      "max_allocation_bytes": 4096, "allocation_probability": 0.65,
      "release_policy": "age_weighted", "alignment_policy": "power_of_two", "warmup_operations": 1000 })";
    auto doc = json::parse(text).value();
    auto c = parse_allocator_config(doc);
    CHECK(c.ok());
    CHECK_EQ(c.value().seed, 20261029U);
    CHECK(c.value().release_policy == ReleasePolicy::AgeWeighted);
    CHECK(c.value().alignment_policy == AlignmentPolicy::PowerOfTwo);
}

TEST(config_parsing_rejects_bad_input) {
    auto parse = [](const char* text) { return parse_allocator_config(json::parse(text).value()); };
    CHECK(!parse(R"({"workload_id":"W01","typo_field":1})").ok());                    // unknown key
    CHECK(!parse(R"({"workload_id":"W99"})").ok());                                     // unknown workload
    CHECK(!parse(R"({"seed":"abc"})").ok());                                            // wrong type
    CHECK(!parse(R"({"operations":0})").ok());
    CHECK(!parse(R"({"release_policy":"random"})").ok());
    CHECK(!parse(R"({"min_allocation_bytes":100,"max_allocation_bytes":50})").ok());
    CHECK(!parse(R"({"allocation_probability":1.5})").ok());
    CHECK(!parse(R"({"operations":100,"warmup_operations":100})").ok());
    CHECK(!parse(R"({"heap_size_bytes":100})").ok());                                   // not a multiple of 16
    CHECK(!parse_allocator_config(json::parse("[1,2]").value()).ok());                  // not an object
    CHECK(parse("{}").ok());                                                            // all defaults are valid
}

TEST(allocation_trace_text_round_trip_and_errors) {
    auto trace = generate_allocation_trace(config_for("W02")).value();
    auto reparsed = parse_allocation_trace(format_allocation_trace(trace));
    CHECK(reparsed.ok());
    CHECK_EQ(reparsed.value().checksum(), trace.checksum());
    CHECK(!parse_allocation_trace("A 10\n").ok());
    CHECK(!parse_allocation_trace("X 1 2\n").ok());
    CHECK(!parse_allocation_trace("F 0\n").ok());                // frees something never allocated
    CHECK(!parse_allocation_trace("A 8 16\nF 0\nF 0\n").ok());   // freed twice
    CHECK(!parse_allocation_trace("A 0 16\n").ok());             // zero size
    CHECK(parse_allocation_trace("# only a comment\n\n").ok());
}

TEST(page_trace_patterns) {
    VmWorkloadConfig c;
    c.accesses = 100;
    c.virtual_pages = 32;
    c.frames = {4};

    c.pattern = PagePattern::Sequential;
    auto seq = generate_page_trace(c).value();
    for (std::size_t i = 0; i < seq.accesses.size(); ++i) CHECK_EQ(seq.accesses[i].vpn, i % 32);

    c.pattern = PagePattern::RepeatedLoop;
    c.loop_pages = 5;
    auto loop = generate_page_trace(c).value();
    for (std::size_t i = 0; i < loop.accesses.size(); ++i) CHECK_EQ(loop.accesses[i].vpn, i % 5);

    c.pattern = PagePattern::Adversarial;
    c.adversarial_frames = 4;
    auto adv = generate_page_trace(c).value();
    for (std::size_t i = 0; i < adv.accesses.size(); ++i) CHECK_EQ(adv.accesses[i].vpn, i % 5);  // frames + 1

    c.pattern = PagePattern::WorkingSetShift;
    c.working_set_pages = 4;
    c.phase_length = 10;
    auto shift = generate_page_trace(c).value();
    for (std::size_t i = 0; i < shift.accesses.size(); ++i) {
        const std::uint64_t window = (i / 10) % (32 / 4);
        CHECK(shift.accesses[i].vpn >= window * 4 && shift.accesses[i].vpn < window * 4 + 4);
    }

    c.pattern = PagePattern::Random;
    auto r1 = generate_page_trace(c).value();
    auto r2 = generate_page_trace(c).value();
    CHECK_EQ(r1.checksum(), r2.checksum());
    for (const auto& a : r1.accesses) CHECK(a.vpn < 32);
}

TEST(page_trace_parsing) {
    auto t = parse_page_trace("# comment\n7, 0, 1w\n2 3W\n");
    CHECK(t.ok());
    CHECK_EQ(t.value().accesses.size(), 5U);
    CHECK(t.value().accesses[2].write);
    CHECK(!t.value().accesses[0].write);
    CHECK(!parse_page_trace("1 x 3").ok());
    CHECK(!parse_page_trace("w").ok());
    auto round = parse_page_trace(format_page_trace(t.value()));
    CHECK_EQ(round.value().checksum(), t.value().checksum());
}

TEST(vm_config_validation) {
    auto parse = [](const char* text) { return parse_vm_config(json::parse(text).value()); };
    CHECK(parse(R"({"pattern":"random","frames":[4,8]})").ok());
    CHECK(parse(R"({"frames":4})").ok());                       // scalar accepted as a one-element list
    CHECK(!parse(R"({"pattern":"zigzag"})").ok());
    CHECK(!parse(R"({"page_size":1000})").ok());
    CHECK(!parse(R"({"frames":[]})").ok());
    CHECK(!parse(R"({"frames":[0]})").ok());
    CHECK(!parse(R"({"nope":1})").ok());
    CHECK(!parse(R"({"pattern":"adversarial","virtual_pages":4,"frames":[8]})").ok());
}
