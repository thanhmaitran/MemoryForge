#include <set>

#include "memoryforge/common/rng.hpp"
#include "memoryforge/vm/virtual_memory.hpp"
#include "test_framework.hpp"

using namespace memoryforge;

namespace {

std::vector<Vpn> random_trace(Rng& rng, std::size_t length, std::uint64_t pages) {
    std::vector<Vpn> trace;
    Vpn hot = 0;
    for (std::size_t i = 0; i < length; ++i) {
        if (rng.chance(0.02)) hot = rng.uniform(0, pages - 1);  // occasional locality shift
        trace.push_back(rng.chance(0.7) ? (hot + rng.uniform(0, 3)) % pages : rng.uniform(0, pages - 1));
    }
    return trace;
}

std::uint64_t run(ReplacementPolicyKind kind, std::uint64_t frames, const std::vector<Vpn>& trace, Rng& rng) {
    auto policy = make_policy(kind, frames, trace);
    auto vm = std::move(VirtualMemory::create(VmConfig{4096, 48, frames}, std::move(policy).value())).value();
    for (const Vpn vpn : trace) {
        const auto type = rng.chance(0.3) ? AccessType::Write : AccessType::Read;
        CHECK(vm.access(vpn * 4096 + rng.uniform(0, 4095), type).ok());
        const auto problem = vm.diagnose();
        CHECK(!problem.has_value());
        if (problem) return 0;
    }
    return vm.stats().page_faults;
}

}  // namespace

TEST(property_vm_invariants_and_opt_lower_bound) {
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        Rng rng(seed);
        const auto trace = random_trace(rng, 600, 48);
        const std::set<Vpn> distinct(trace.begin(), trace.end());
        const std::uint64_t frames = rng.uniform(1, 12);
        const auto opt = run(ReplacementPolicyKind::Optimal, frames, trace, rng);
        CHECK(opt >= std::min<std::uint64_t>(distinct.size(), 1));
        CHECK(opt >= distinct.size());  // every distinct page must be loaded at least once
        for (const auto kind : {ReplacementPolicyKind::Fifo, ReplacementPolicyKind::Lru, ReplacementPolicyKind::Clock}) {
            CHECK(opt <= run(kind, frames, trace, rng));
        }
    }
}

TEST(property_lru_has_the_inclusion_property) {
    // LRU never faults more when given more frames (it is a stack algorithm).
    for (std::uint64_t seed = 100; seed < 115; ++seed) {
        Rng rng(seed);
        const auto trace = random_trace(rng, 500, 48);
        std::uint64_t previous = ~std::uint64_t{0};
        for (std::uint64_t frames = 1; frames <= 10; ++frames) {
            const auto faults = run(ReplacementPolicyKind::Lru, frames, trace, rng);
            CHECK(faults <= previous);
            previous = faults;
        }
    }
}

TEST(property_enough_frames_means_only_compulsory_faults) {
    Rng rng(7);
    const auto trace = random_trace(rng, 400, 48);
    const std::set<Vpn> distinct(trace.begin(), trace.end());
    for (const auto kind : {ReplacementPolicyKind::Fifo, ReplacementPolicyKind::Lru, ReplacementPolicyKind::Optimal,
                            ReplacementPolicyKind::Clock}) {
        CHECK_EQ(run(kind, 48, trace, rng), distinct.size());
    }
}
