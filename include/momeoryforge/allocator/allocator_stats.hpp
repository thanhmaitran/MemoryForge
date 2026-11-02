#pragma once

#include <cstddef>
#include <cstdint>

namespace memoryforge {

/// Snapshot of allocator state. All byte counts use the published layout
/// convention (see docs/architecture/allocator-design.md).
struct AllocatorStats {
    std::size_t capacity_bytes = 0;
    std::size_t live_requested_bytes = 0;  ///< Sum of the sizes callers asked for.
    std::size_t live_allocated_bytes = 0;  ///< Sum of reserved block sizes (rounded up).
    std::size_t free_bytes = 0;
    std::size_t largest_free_block = 0;
    std::size_t free_block_count = 0;
    std::size_t live_allocation_count = 0;
    std::size_t peak_live_allocated_bytes = 0;

    std::uint64_t allocation_count = 0;    ///< Successful allocations.
    std::uint64_t deallocation_count = 0;  ///< Successful deallocations.
    /// Valid requests that could not be satisfied (out of memory / fragmentation).
    /// Invalid requests (zero size, bad alignment, overflow) are not counted here.
    std::uint64_t failed_allocations = 0;
    /// Free blocks examined while searching, summed over all allocation attempts.
    std::uint64_t search_steps = 0;

    /// Bytes reserved beyond what callers requested (granularity rounding).
    [[nodiscard]] std::size_t internal_overhead_bytes() const noexcept {
        return live_allocated_bytes - live_requested_bytes;
    }
};

}  // namespace memoryforge
