#include <cstdint>
#include <limits>

#include "memoryforge/allocator/free_list_allocator.hpp"
#include "memoryforge/metrics/fragmentation.hpp"
#include "test_framework.hpp"

using namespace memoryforge;

namespace {
std::unique_ptr<MemoryAllocator> make(AllocationStrategy s, std::size_t capacity) {
    auto r = make_allocator(s, capacity);
    return std::move(r).value();
}
ErrorCode code_of(const Result<AllocationHandle>& r) { return r.error().code; }
}  // namespace

TEST(create_rejects_bad_capacity) {
    CHECK(!make_allocator(AllocationStrategy::FirstFit, 0).ok());
    CHECK(!make_allocator(AllocationStrategy::FirstFit, 100).ok());  // not a multiple of 16
    CHECK(make_allocator(AllocationStrategy::FirstFit, 4096).ok());
}

TEST(allocate_rounds_to_granularity_and_tracks_stats) {
    auto a = make(AllocationStrategy::FirstFit, 1024);
    auto h = a->allocate(100, 16);
    CHECK(h.ok());
    CHECK_EQ(h.value().offset, 0U);
    CHECK_EQ(h.value().requested_size, 100U);
    CHECK_EQ(h.value().allocated_size, 112U);
    const auto s = a->stats();
    CHECK_EQ(s.live_requested_bytes, 100U);
    CHECK_EQ(s.live_allocated_bytes, 112U);
    CHECK_EQ(s.internal_overhead_bytes(), 12U);
    CHECK_EQ(s.free_bytes, 912U);
    CHECK_EQ(s.free_block_count, 1U);
    CHECK_EQ(s.largest_free_block, 912U);
    CHECK_EQ(s.allocation_count, 1U);
    CHECK(a->validate());
}

TEST(invalid_requests_report_specific_errors) {
    auto a = make(AllocationStrategy::BestFit, 1024);
    CHECK_EQ(code_of(a->allocate(0, 16)), ErrorCode::ZeroSize);
    CHECK_EQ(code_of(a->allocate(16, 0)), ErrorCode::InvalidAlignment);
    CHECK_EQ(code_of(a->allocate(16, 24)), ErrorCode::InvalidAlignment);
    CHECK_EQ(code_of(a->allocate(16, 8192)), ErrorCode::InvalidAlignment);
    CHECK_EQ(code_of(a->allocate(std::numeric_limits<std::size_t>::max(), 16)), ErrorCode::SizeOverflow);
    CHECK_EQ(a->stats().failed_allocations, 0U);  // invalid requests are not capacity failures
    CHECK_EQ(a->stats().free_bytes, 1024U);
}

TEST(out_of_memory_is_counted_and_leaves_state_unchanged) {
    auto a = make(AllocationStrategy::FirstFit, 1024);
    CHECK_EQ(code_of(a->allocate(2048, 16)), ErrorCode::OutOfMemory);
    CHECK(a->allocate(1024, 16).ok());  // exact fit
    CHECK_EQ(a->stats().free_bytes, 0U);
    CHECK_EQ(code_of(a->allocate(16, 16)), ErrorCode::OutOfMemory);
    CHECK_EQ(a->stats().failed_allocations, 2U);
    CHECK(a->validate());
}

TEST(invalid_and_double_free_are_distinguished) {
    auto a = make(AllocationStrategy::FirstFit, 1024);
    auto h = a->allocate(64, 16).value();
    CHECK_EQ(a->deallocate(0).error().code, ErrorCode::InvalidHandle);
    CHECK_EQ(a->deallocate(999).error().code, ErrorCode::InvalidHandle);
    CHECK(a->deallocate(h.id).ok());
    CHECK_EQ(a->deallocate(h.id).error().code, ErrorCode::DoubleFree);
    CHECK(a->validate());
    CHECK_EQ(a->stats().deallocation_count, 1U);
}

TEST(alignment_is_honoured_and_gap_stays_free) {
    auto a = make(AllocationStrategy::FirstFit, 4096);
    CHECK(a->allocate(16, 16).ok());          // occupies [0,16)
    auto h = a->allocate(32, 256).value();     // must start at 256
    CHECK_EQ(h.offset, 256U);
    CHECK_EQ(h.offset % 256, 0U);
    const auto s = a->stats();
    CHECK_EQ(s.free_block_count, 2U);          // gap [16,256) and the tail
    CHECK_EQ(s.free_bytes, 4096U - 16U - 32U);
    CHECK(a->validate());
    // The gap is reusable by a small allocation.
    CHECK_EQ(a->allocate(48, 16).value().offset, 16U);
}

TEST(splitting_and_coalescing) {
    auto a = make(AllocationStrategy::FirstFit, 1024);
    auto x = a->allocate(256, 16).value();
    auto y = a->allocate(256, 16).value();
    auto z = a->allocate(256, 16).value();
    CHECK_EQ(a->stats().free_block_count, 1U);   // only the tail
    CHECK(a->deallocate(y.id).ok());
    CHECK_EQ(a->stats().free_block_count, 2U);
    CHECK(a->deallocate(x.id).ok());             // merges with y's hole
    CHECK_EQ(a->stats().free_block_count, 2U);
    CHECK_EQ(a->stats().largest_free_block, 512U);
    CHECK(a->deallocate(z.id).ok());             // merges everything
    const auto s = a->stats();
    CHECK_EQ(s.free_block_count, 1U);
    CHECK_EQ(s.largest_free_block, 1024U);
    CHECK(a->validate());
}

TEST(freed_memory_is_reused) {
    auto a = make(AllocationStrategy::FirstFit, 1024);
    CHECK(a->allocate(256, 16).ok());
    auto mid = a->allocate(256, 16).value();
    CHECK(a->allocate(256, 16).ok());
    CHECK(a->deallocate(mid.id).ok());
    CHECK_EQ(a->allocate(256, 16).value().offset, mid.offset);
}

TEST(strategies_choose_different_blocks) {
    // Layout: hole64 | pin | hole32 | pin | hole128 | pin | big tail
    struct Case {
        AllocationStrategy strategy;
        std::size_t expected_offset;
    };
    const Case cases[] = {{AllocationStrategy::FirstFit, 0}, {AllocationStrategy::BestFit, 80}, {AllocationStrategy::WorstFit, 272}};
    for (const Case& c : cases) {
        auto a = make(c.strategy, 4096);
        auto h1 = a->allocate(64, 16).value();
        CHECK(a->allocate(16, 16).ok());
        auto h2 = a->allocate(32, 16).value();
        CHECK(a->allocate(16, 16).ok());
        auto h3 = a->allocate(128, 16).value();
        CHECK(a->allocate(16, 16).ok());
        CHECK(a->deallocate(h1.id).ok());
        CHECK(a->deallocate(h2.id).ok());
        CHECK(a->deallocate(h3.id).ok());
        CHECK_EQ(a->allocate(32, 16).value().offset, c.expected_offset);
        CHECK(a->validate());
    }
}

TEST(search_steps_reflect_policy_cost) {
    // First Fit stops at the first fitting block, Best/Worst scan the whole list.
    auto first = make(AllocationStrategy::FirstFit, 4096);
    auto best = make(AllocationStrategy::BestFit, 4096);
    for (auto* a : {first.get(), best.get()}) {
        std::vector<std::uint64_t> ids;
        for (int i = 0; i < 8; ++i) {
            ids.push_back(a->allocate(64, 16).value().id);
            CHECK(a->allocate(16, 16).ok());
        }
        for (const auto id : ids) CHECK(a->deallocate(id).ok());  // 8 holes + tail
        CHECK(a->allocate(64, 16).ok());
    }
    CHECK(first->stats().search_steps < best->stats().search_steps);
}

TEST(memory_view_is_writable_and_disjoint) {
    auto a = make(AllocationStrategy::FirstFit, 1024);
    auto h1 = a->allocate(40, 16).value();
    auto h2 = a->allocate(40, 16).value();
    auto m1 = a->memory(h1.id);
    auto m2 = a->memory(h2.id);
    CHECK_EQ(m1.size(), 40U);
    for (auto& b : m1) b = std::byte{0xAA};
    for (auto& b : m2) b = std::byte{0xBB};
    for (const auto b : m1) CHECK(b == std::byte{0xAA});
    CHECK_EQ(reinterpret_cast<std::uintptr_t>(m1.data()) % 16, 0U);
    CHECK(a->memory(12345).empty());
}

TEST(external_fragmentation_indicator) {
    // One 512-byte hole versus eight 64-byte holes: same free bytes, very different usability.
    auto a = make(AllocationStrategy::FirstFit, 1024);
    std::vector<std::uint64_t> ids;
    for (int i = 0; i < 16; ++i) ids.push_back(a->allocate(64, 16).value().id);
    CHECK(!external_fragmentation_indicator(a->stats()).has_value());  // full heap: undefined, not 0%
    for (int i = 0; i < 16; i += 2) CHECK(a->deallocate(ids[static_cast<std::size_t>(i)]).ok());
    const auto s = a->stats();
    CHECK_EQ(s.free_bytes, 512U);
    CHECK_EQ(s.free_block_count, 8U);
    CHECK_EQ(s.largest_free_block, 64U);
    CHECK(*external_fragmentation_indicator(s) > 0.874 && *external_fragmentation_indicator(s) < 0.876);
    CHECK_EQ(code_of(a->allocate(128, 16)), ErrorCode::OutOfMemory);  // enough bytes, no usable block
    for (int i = 1; i < 16; i += 2) CHECK(a->deallocate(ids[static_cast<std::size_t>(i)]).ok());
    CHECK(*external_fragmentation_indicator(a->stats()) == 0.0);
}

TEST(peak_usage_is_tracked) {
    auto a = make(AllocationStrategy::FirstFit, 1024);
    auto h = a->allocate(512, 16).value();
    CHECK(a->deallocate(h.id).ok());
    CHECK(a->allocate(64, 16).ok());
    CHECK_EQ(a->stats().peak_live_allocated_bytes, 512U);
}
