#include <algorithm>
#include <map>

#include "memoryforge/allocator/free_list_allocator.hpp"
#include "memoryforge/common/rng.hpp"
#include "test_framework.hpp"

using namespace memoryforge;

namespace {

struct Model {
    std::size_t offset;
    std::size_t allocated;
    std::size_t requested;
};

// Random operation sequences checked against an independent model plus the
// allocator's own validator after every single step. Every live allocation is
// filled with a unique byte pattern that must survive until it is freed; an
// overlap between blocks would corrupt it.
void run_sequence(AllocationStrategy strategy, std::uint64_t seed) {
    constexpr std::size_t kCapacity = 64 * 1024;
    auto a = make_allocator(strategy, kCapacity).value();
    Rng rng(seed);
    std::map<std::uint64_t, Model> live;
    const std::size_t alignments[] = {1, 8, 16, 32, 64, 256, 4096};

    for (int step = 0; step < 3000; ++step) {
        if (live.empty() || rng.chance(0.55)) {
            const auto size = static_cast<std::size_t>(rng.uniform(1, 3000));
            const std::size_t alignment = alignments[rng.uniform(0, 6)];
            auto r = a->allocate(size, alignment);
            if (r) {
                const AllocationHandle h = r.value();
                CHECK(h.offset + h.allocated_size <= kCapacity);
                CHECK_EQ(h.offset % std::max<std::size_t>(alignment, 16), 0U);
                CHECK(h.allocated_size >= size);
                for (const auto& [id, m] : live) {  // independent overlap check
                    const bool disjoint = h.offset + h.allocated_size <= m.offset || m.offset + m.allocated <= h.offset;
                    CHECK(disjoint);
                    static_cast<void>(id);
                }
                auto bytes = a->memory(h.id);
                CHECK_EQ(bytes.size(), size);
                std::fill(bytes.begin(), bytes.end(), static_cast<std::byte>(h.id & 0xFFU));
                live[h.id] = {h.offset, h.allocated_size, size};
            } else {
                CHECK(r.error().code == ErrorCode::OutOfMemory);
            }
        } else {
            auto it = live.begin();
            std::advance(it, static_cast<std::ptrdiff_t>(rng.uniform(0, live.size() - 1)));
            const std::uint64_t id = it->first;
            for (const std::byte b : a->memory(id)) CHECK(b == static_cast<std::byte>(id & 0xFFU));
            CHECK(a->deallocate(id).ok());
            CHECK_EQ(a->deallocate(id).error().code, ErrorCode::DoubleFree);
            live.erase(it);
        }
        const auto problem = a->diagnose();
        CHECK(!problem.has_value());
        if (problem) {
            std::cerr << "    step " << step << ", seed " << seed << ": " << *problem << '\n';
            return;
        }
    }
    std::size_t expected_allocated = 0;
    for (const auto& [id, m] : live) {
        expected_allocated += m.allocated;
        static_cast<void>(id);
    }
    CHECK_EQ(a->stats().live_allocated_bytes, expected_allocated);
    CHECK_EQ(a->stats().live_allocation_count, live.size());

    for (const auto& [id, m] : live) {
        CHECK(a->deallocate(id).ok());
        static_cast<void>(m);
    }
    const auto s = a->stats();
    CHECK_EQ(s.free_block_count, 1U);  // everything coalesced back: no logical leak
    CHECK_EQ(s.largest_free_block, kCapacity);
    CHECK(a->validate());
}

}  // namespace

TEST(property_random_sequences_first_fit) {
    for (std::uint64_t seed = 1; seed <= 12; ++seed) run_sequence(AllocationStrategy::FirstFit, seed);
}
TEST(property_random_sequences_best_fit) {
    for (std::uint64_t seed = 1; seed <= 12; ++seed) run_sequence(AllocationStrategy::BestFit, seed);
}
TEST(property_random_sequences_worst_fit) {
    for (std::uint64_t seed = 1; seed <= 12; ++seed) run_sequence(AllocationStrategy::WorstFit, seed);
}
