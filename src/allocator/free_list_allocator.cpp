#include "memoryforge/allocator/free_list_allocator.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace memoryforge {
namespace {

constexpr bool is_power_of_two(std::size_t v) noexcept { return v != 0 && (v & (v - 1)) == 0; }
constexpr std::size_t align_up(std::size_t v, std::size_t a) noexcept {
    return (v + (a - 1)) & ~(a - 1);
}

}  // namespace

FreeListAllocator::FreeListAllocator(std::size_t capacity, AllocationStrategy strategy,
                                     std::unique_ptr<std::byte[], ArenaDeleter> arena)
    : capacity_(capacity), strategy_(strategy), arena_(std::move(arena)) {
    free_blocks_.emplace(0, capacity_);
}

Result<std::unique_ptr<FreeListAllocator>> FreeListAllocator::create(
    std::size_t capacity_bytes, AllocationStrategy strategy) {
    if (capacity_bytes == 0 || capacity_bytes % kGranularity != 0) {
        return make_error(ErrorCode::InvalidArgument,
                          "capacity must be a non-zero multiple of " +
                              std::to_string(kGranularity) + " bytes");
    }
    std::byte* raw = nullptr;
    try {
        raw = static_cast<std::byte*>(
            ::operator new(capacity_bytes, std::align_val_t{kMaxAlignment}));
    } catch (const std::bad_alloc&) {
        return make_error(ErrorCode::OutOfMemory, "could not reserve the arena buffer");
    }
    std::unique_ptr<std::byte[], ArenaDeleter> arena(raw);
    return std::unique_ptr<FreeListAllocator>(
        new FreeListAllocator(capacity_bytes, strategy, std::move(arena)));
}

Result<AllocationHandle> FreeListAllocator::allocate(std::size_t bytes, std::size_t alignment) {
    if (bytes == 0) return make_error(ErrorCode::ZeroSize, "allocation size must be non-zero");
    if (!is_power_of_two(alignment) || alignment > kMaxAlignment) {
        return make_error(ErrorCode::InvalidAlignment,
                          "alignment must be a power of two no larger than " +
                              std::to_string(kMaxAlignment));
    }
    if (bytes > std::numeric_limits<std::size_t>::max() - (kGranularity - 1)) {
        return make_error(ErrorCode::SizeOverflow, "allocation size overflows size_t");
    }
    alignment = std::max(alignment, kGranularity);
    const std::size_t allocated = align_up(bytes, kGranularity);

    if (allocated > capacity_) {
        ++failed_allocations_;
        return make_error(ErrorCode::OutOfMemory, "request exceeds arena capacity");
    }

    // Search. Every strategy scans the same address-ordered list; only the
    // selection rule differs. First Fit stops at its first match.
    auto chosen = free_blocks_.end();
    std::size_t chosen_size = 0;
    for (auto it = free_blocks_.begin(); it != free_blocks_.end(); ++it) {
        ++search_steps_;
        const std::size_t offset = it->first;
        const std::size_t size = it->second;
        const std::size_t gap = align_up(offset, alignment) - offset;
        if (gap > size || size - gap < allocated) continue;

        if (strategy_ == AllocationStrategy::FirstFit) {
            chosen = it;
            break;
        }
        const bool better = (chosen == free_blocks_.end()) ||
                            (strategy_ == AllocationStrategy::BestFit ? size < chosen_size
                                                                      : size > chosen_size);
        if (better) {
            chosen = it;
            chosen_size = size;
        }
    }

    if (chosen == free_blocks_.end()) {
        ++failed_allocations_;
        return make_error(ErrorCode::OutOfMemory,
                          "no free block can satisfy the request (size " +
                              std::to_string(allocated) + ", alignment " +
                              std::to_string(alignment) + ")");
    }

    const std::size_t block_offset = chosen->first;
    const std::size_t block_size = chosen->second;
    const std::size_t aligned = align_up(block_offset, alignment);
    const std::size_t head_gap = aligned - block_offset;
    const std::size_t tail = block_size - head_gap - allocated;

    // Both leftovers sit immediately before the block that followed `chosen`.
    const auto following = free_blocks_.erase(chosen);
    if (head_gap > 0) free_blocks_.emplace_hint(following, block_offset, head_gap);
    if (tail > 0) free_blocks_.emplace_hint(following, aligned + allocated, tail);

    const std::uint64_t id = next_id_++;
    live_.emplace(id, LiveBlock{aligned, bytes, allocated, alignment});
    live_requested_ += bytes;
    live_allocated_ += allocated;
    peak_live_allocated_ = std::max(peak_live_allocated_, live_allocated_);
    ++allocation_count_;
    return AllocationHandle{id, aligned, bytes, allocated};
}

Status FreeListAllocator::deallocate(std::uint64_t allocation_id) {
    const auto it = live_.find(allocation_id);
    if (it == live_.end()) {
        if (allocation_id == 0 || allocation_id >= next_id_) {
            return make_error(ErrorCode::InvalidHandle,
                              "allocation id " + std::to_string(allocation_id) + " was never issued");
        }
        return make_error(ErrorCode::DoubleFree,
                          "allocation id " + std::to_string(allocation_id) + " is already freed");
    }
    const LiveBlock block = it->second;
    live_.erase(it);
    live_requested_ -= block.requested;
    live_allocated_ -= block.allocated;
    ++deallocation_count_;

    std::size_t start = block.offset;
    std::size_t length = block.allocated;
    auto next = free_blocks_.lower_bound(block.offset);
    if (next != free_blocks_.end() && next->first == block.offset + block.allocated) {
        length += next->second;
        next = free_blocks_.erase(next);
    }
    if (next != free_blocks_.begin()) {
        const auto prev = std::prev(next);
        if (prev->first + prev->second == block.offset) {
            start = prev->first;
            length += prev->second;
            next = free_blocks_.erase(prev);
        }
    }
    free_blocks_.emplace_hint(next, start, length);
    return Status::success();
}

AllocatorStats FreeListAllocator::stats() const {
    AllocatorStats s;
    s.capacity_bytes = capacity_;
    s.live_requested_bytes = live_requested_;
    s.live_allocated_bytes = live_allocated_;
    s.free_bytes = capacity_ - live_allocated_;
    s.free_block_count = free_blocks_.size();
    for (const auto& block : free_blocks_) s.largest_free_block = std::max(s.largest_free_block, block.second);
    s.live_allocation_count = live_.size();
    s.peak_live_allocated_bytes = peak_live_allocated_;
    s.allocation_count = allocation_count_;
    s.deallocation_count = deallocation_count_;
    s.failed_allocations = failed_allocations_;
    s.search_steps = search_steps_;
    return s;
}

std::span<std::byte> FreeListAllocator::memory(std::uint64_t allocation_id) noexcept {
    const auto it = live_.find(allocation_id);
    if (it == live_.end()) return {};
    return {arena_.get() + it->second.offset, it->second.requested};
}

std::optional<std::string> FreeListAllocator::diagnose() const {
    struct Interval {
        std::size_t offset;
        std::size_t size;
        bool is_free;
    };
    std::vector<Interval> intervals;
    intervals.reserve(free_blocks_.size() + live_.size());

    std::size_t free_sum = 0;
    for (const auto& [offset, size] : free_blocks_) {
        if (size == 0) return "free block at " + std::to_string(offset) + " has zero size";
        if (offset % kGranularity != 0 || size % kGranularity != 0) {
            return "free block at " + std::to_string(offset) + " violates granularity";
        }
        free_sum += size;
        intervals.push_back({offset, size, true});
    }

    std::size_t requested_sum = 0;
    std::size_t allocated_sum = 0;
    const auto base = reinterpret_cast<std::uintptr_t>(arena_.get());
    for (const auto& [id, block] : live_) {
        if (id == 0 || id >= next_id_) return "live allocation has an id that was never issued";
        if (block.requested == 0 || block.requested > block.allocated) {
            return "allocation " + std::to_string(id) + ": requested size exceeds allocated size";
        }
        if (block.allocated != align_up(block.requested, kGranularity)) {
            return "allocation " + std::to_string(id) + ": allocated size breaks the layout convention";
        }
        if (block.offset % block.alignment != 0 || (base + block.offset) % block.alignment != 0) {
            return "allocation " + std::to_string(id) + ": misaligned for its requested alignment";
        }
        requested_sum += block.requested;
        allocated_sum += block.allocated;
        intervals.push_back({block.offset, block.allocated, false});
    }

    if (requested_sum != live_requested_) return "live_requested counter does not match blocks";
    if (allocated_sum != live_allocated_) return "live_allocated counter does not match blocks";
    if (live_requested_ > live_allocated_) return "live requested bytes exceed live allocated bytes";
    if (free_sum + allocated_sum != capacity_) return "free + allocated bytes do not equal capacity";

    std::sort(intervals.begin(), intervals.end(),
              [](const Interval& a, const Interval& b) { return a.offset < b.offset; });
    std::size_t cursor = 0;
    bool previous_free = false;
    for (const Interval& iv : intervals) {
        if (iv.offset < cursor) return "blocks overlap near offset " + std::to_string(iv.offset);
        if (iv.offset > cursor) return "gap in block tiling at offset " + std::to_string(cursor);
        if (iv.is_free && previous_free) {
            return "adjacent free blocks were not coalesced at offset " + std::to_string(iv.offset);
        }
        cursor = iv.offset + iv.size;
        previous_free = iv.is_free;
    }
    if (cursor != capacity_) return "blocks do not cover the whole arena";
    return std::nullopt;
}

Result<std::unique_ptr<MemoryAllocator>> make_allocator(AllocationStrategy strategy,
                                                        std::size_t capacity_bytes) {
    auto created = FreeListAllocator::create(capacity_bytes, strategy);
    if (!created) return created.error();
    return std::unique_ptr<MemoryAllocator>(std::move(created).value());
}

}  // namespace memoryforge
