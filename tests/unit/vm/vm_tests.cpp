#include "memoryforge/vm/virtual_memory.hpp"
#include "test_framework.hpp"

using namespace memoryforge;

namespace {

VirtualMemory make_vm(ReplacementPolicyKind kind, std::uint64_t frames, const std::vector<Vpn>& trace = {},
                      std::uint64_t pages = 64) {
    auto policy = make_policy(kind, frames, trace);
    return std::move(VirtualMemory::create(VmConfig{4096, pages, frames}, std::move(policy).value())).value();
}

std::uint64_t run_faults(ReplacementPolicyKind kind, std::uint64_t frames, const std::vector<Vpn>& trace) {
    VirtualMemory vm = make_vm(kind, frames, trace);
    for (const Vpn vpn : trace) {
        CHECK(vm.access(vpn * 4096, AccessType::Read).ok());
        CHECK(vm.validate());
    }
    return vm.stats().page_faults;
}

const std::vector<Vpn> kTextbook13 = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2};
const std::vector<Vpn> kTextbook20 = {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};
const std::vector<Vpn> kBelady = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};

}  // namespace

TEST(address_translation_formula) {
    VirtualMemory vm = make_vm(ReplacementPolicyKind::Fifo, 4, {});
    // VA = 3 * 4096 + 17 -> VPN 3, offset 17. First fault takes frame 0.
    auto first = vm.access(3 * 4096 + 17, AccessType::Read).value();
    CHECK_EQ(first.vpn, 3U);
    CHECK_EQ(first.offset, 17U);
    CHECK(first.page_fault);
    CHECK(first.compulsory);
    CHECK_EQ(first.pfn, 0U);
    CHECK_EQ(first.physical_address, 0U * 4096 + 17);
    // Second page takes frame 1: PA = 1 * 4096 + 4095.
    auto second = vm.access(9 * 4096 + 4095, AccessType::Read).value();
    CHECK_EQ(second.pfn, 1U);
    CHECK_EQ(second.physical_address, 4096U + 4095U);
    // Re-access page 3: hit, same frame.
    auto again = vm.access(3 * 4096 + 100, AccessType::Read).value();
    CHECK(!again.page_fault);
    CHECK_EQ(again.physical_address, 100U);
    CHECK_EQ(vm.translate(3 * 4096 + 100).value(), 100U);
    CHECK(!vm.translate(40 * 4096).has_value());  // not resident, translate has no side effects
    CHECK_EQ(vm.stats().accesses, 3U);
}

TEST(out_of_range_address_changes_nothing) {
    VirtualMemory vm = make_vm(ReplacementPolicyKind::Lru, 2, {}, 8);
    CHECK_EQ(vm.access(8 * 4096, AccessType::Read).error().code, ErrorCode::OutOfRange);
    CHECK_EQ(vm.stats().accesses, 0U);
    CHECK(vm.validate());
}

TEST(no_replacement_while_free_frames_exist) {
    VirtualMemory vm = make_vm(ReplacementPolicyKind::Lru, 4, {});
    for (Vpn p = 0; p < 4; ++p) {
        auto r = vm.access(p * 4096, AccessType::Read).value();
        CHECK(!r.evicted.has_value());
    }
    CHECK_EQ(vm.stats().replacements, 0U);
    CHECK_EQ(vm.resident_pages(), 4U);
    auto r = vm.access(10 * 4096, AccessType::Read).value();
    CHECK(r.evicted.has_value());
    CHECK_EQ(vm.stats().replacements, 1U);
}

TEST(textbook_trace_fault_counts) {
    CHECK_EQ(run_faults(ReplacementPolicyKind::Fifo, 3, kTextbook13), 10U);
    CHECK_EQ(run_faults(ReplacementPolicyKind::Lru, 3, kTextbook13), 9U);
    CHECK_EQ(run_faults(ReplacementPolicyKind::Optimal, 3, kTextbook13), 7U);
    CHECK_EQ(run_faults(ReplacementPolicyKind::Fifo, 3, kTextbook20), 15U);
    CHECK_EQ(run_faults(ReplacementPolicyKind::Lru, 3, kTextbook20), 12U);
    CHECK_EQ(run_faults(ReplacementPolicyKind::Optimal, 3, kTextbook20), 9U);
}

TEST(fifo_exhibits_beladys_anomaly_but_lru_does_not) {
    CHECK_EQ(run_faults(ReplacementPolicyKind::Fifo, 3, kBelady), 9U);
    CHECK_EQ(run_faults(ReplacementPolicyKind::Fifo, 4, kBelady), 10U);  // more frames, more faults
    CHECK(run_faults(ReplacementPolicyKind::Lru, 4, kBelady) <= run_faults(ReplacementPolicyKind::Lru, 3, kBelady));
}

TEST(fifo_evicts_in_load_order_and_ignores_hits) {
    VirtualMemory vm = make_vm(ReplacementPolicyKind::Fifo, 2, {});
    vm.access(1 * 4096, AccessType::Read);
    vm.access(2 * 4096, AccessType::Read);
    vm.access(1 * 4096, AccessType::Read);  // hit must not refresh page 1
    auto r = vm.access(3 * 4096, AccessType::Read).value();
    CHECK_EQ(r.evicted.value(), 1U);
}

TEST(lru_evicts_least_recently_used) {
    VirtualMemory vm = make_vm(ReplacementPolicyKind::Lru, 2, {});
    vm.access(1 * 4096, AccessType::Read);
    vm.access(2 * 4096, AccessType::Read);
    vm.access(1 * 4096, AccessType::Read);  // refresh page 1
    auto r = vm.access(3 * 4096, AccessType::Read).value();
    CHECK_EQ(r.evicted.value(), 2U);
}

TEST(clock_gives_a_second_chance) {
    // Hand-traced with 2 frames. After 1,2 both reference bits are set. Page 3 makes the hand clear both bits and
    // evict page 1. The hit on 2 sets its bit again, so page 4 makes the hand clear 3 and 2 and then evict 2.
    VirtualMemory vm = make_vm(ReplacementPolicyKind::Clock, 2, {});
    vm.access(1 * 4096, AccessType::Read);
    vm.access(2 * 4096, AccessType::Read);
    CHECK_EQ(vm.access(3 * 4096, AccessType::Read).value().evicted.value(), 1U);
    CHECK(!vm.access(2 * 4096, AccessType::Read).value().page_fault);
    CHECK_EQ(vm.access(4 * 4096, AccessType::Read).value().evicted.value(), 2U);
    CHECK(vm.validate());
}

TEST(optimal_never_loses_to_online_policies_on_known_traces) {
    for (const auto* trace : {&kTextbook13, &kTextbook20, &kBelady}) {
        for (std::uint64_t frames = 1; frames <= 5; ++frames) {
            const auto opt = run_faults(ReplacementPolicyKind::Optimal, frames, *trace);
            for (const auto kind : {ReplacementPolicyKind::Fifo, ReplacementPolicyKind::Lru, ReplacementPolicyKind::Clock}) {
                CHECK(opt <= run_faults(kind, frames, *trace));
            }
        }
    }
}

TEST(optimal_requires_a_trace_and_detects_a_different_one) {
    CHECK(!make_policy(ReplacementPolicyKind::Optimal, 3, {}).ok());
    const std::vector<Vpn> declared = {1, 2, 3};
    VirtualMemory vm = make_vm(ReplacementPolicyKind::Optimal, 2, declared);
    vm.access(1 * 4096, AccessType::Read);
    CHECK(vm.validate());
    vm.access(9 * 4096, AccessType::Read);  // not what the policy was told
    CHECK(!vm.validate());
}

TEST(dirty_pages_are_written_back_on_eviction) {
    VirtualMemory vm = make_vm(ReplacementPolicyKind::Fifo, 1, {});
    vm.access(0, AccessType::Write);
    auto r = vm.access(1 * 4096, AccessType::Read).value();  // evicts dirty page 0
    CHECK(r.wrote_back);
    auto r2 = vm.access(2 * 4096, AccessType::Read).value();  // evicts clean page 1
    CHECK(!r2.wrote_back);
    CHECK_EQ(vm.stats().writebacks, 1U);
    CHECK_EQ(vm.stats().writes, 1U);
    CHECK(vm.validate());
}

TEST(compulsory_faults_count_first_touches_only) {
    VirtualMemory vm = make_vm(ReplacementPolicyKind::Fifo, 1, {});
    vm.access(0, AccessType::Read);
    vm.access(4096, AccessType::Read);
    vm.access(0, AccessType::Read);  // fault again, but not compulsory
    CHECK_EQ(vm.stats().page_faults, 3U);
    CHECK_EQ(vm.stats().compulsory_faults, 2U);
}

TEST(create_validates_geometry) {
    auto policy = [] { return std::move(make_policy(ReplacementPolicyKind::Fifo, 2).value()); };
    CHECK(!VirtualMemory::create(VmConfig{1000, 8, 2}, policy()).ok());  // page size not a power of two
    CHECK(!VirtualMemory::create(VmConfig{4096, 0, 2}, policy()).ok());
    CHECK(!VirtualMemory::create(VmConfig{4096, 8, 0}, policy()).ok());
    CHECK(!VirtualMemory::create(VmConfig{4096, 8, 2}, nullptr).ok());
}
