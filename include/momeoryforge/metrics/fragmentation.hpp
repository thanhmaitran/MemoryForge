#pragma once

#include <optional>

#include "memoryforge/allocator/allocator_stats.hpp"

namespace memoryforge {

/// External fragmentation indicator 1 - L/F, where L is the largest free block
/// and F the total free bytes. It describes the layout of free space, not a
/// universal definition of fragmentation. Undefined (nullopt) when F == 0, so
/// a full heap is never reported as "0% fragmented".
[[nodiscard]] inline std::optional<double> external_fragmentation_indicator(
    const AllocatorStats& s) noexcept {
    if (s.free_bytes == 0) return std::nullopt;
    return 1.0 - static_cast<double>(s.largest_free_block) / static_cast<double>(s.free_bytes);
}

/// Internal overhead as a fraction of allocated bytes; undefined when nothing is allocated.
[[nodiscard]] inline std::optional<double> internal_overhead_ratio(
    const AllocatorStats& s) noexcept {
    if (s.live_allocated_bytes == 0) return std::nullopt;
    return static_cast<double>(s.internal_overhead_bytes()) /
           static_cast<double>(s.live_allocated_bytes);
}

/// Fraction of the arena reserved by live allocations.
[[nodiscard]] inline double utilization(const AllocatorStats& s) noexcept {
    if (s.capacity_bytes == 0) return 0.0;
    return static_cast<double>(s.live_allocated_bytes) / static_cast<double>(s.capacity_bytes);
}

}  // namespace memoryforge
