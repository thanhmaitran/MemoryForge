#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "memoryforge/allocator/allocator_stats.hpp"
#include "memoryforge/common/result.hpp"

namespace memoryforge {

enum class AllocationStrategy { FirstFit, BestFit, WorstFit };

constexpr std::string_view to_string(AllocationStrategy s) noexcept {
    switch (s) {
        case AllocationStrategy::FirstFit: return "first-fit";
        case AllocationStrategy::BestFit: return "best-fit";
        case AllocationStrategy::WorstFit: return "worst-fit";
    }
    return "unknown";
}

constexpr std::optional<AllocationStrategy> parse_strategy(std::string_view name) noexcept {
    if (name == "first-fit") return AllocationStrategy::FirstFit;
    if (name == "best-fit") return AllocationStrategy::BestFit;
    if (name == "worst-fit") return AllocationStrategy::WorstFit;
    return std::nullopt;
}

inline constexpr AllocationStrategy kAllStrategies[] = {
    AllocationStrategy::FirstFit, AllocationStrategy::BestFit, AllocationStrategy::WorstFit};

/// Describes one live allocation. `offset` is relative to the arena base.
struct AllocationHandle {
    std::uint64_t id = 0;
    std::size_t offset = 0;
    std::size_t requested_size = 0;
    std::size_t allocated_size = 0;
};

/// Contract for an arena allocator.
///
/// Failure semantics (never throws for caller errors):
///   allocate():   ZeroSize, InvalidAlignment, SizeOverflow, OutOfMemory
///   deallocate(): InvalidHandle (id never issued), DoubleFree (id already freed)
/// A failed call leaves the allocator state unchanged except for counters.
/// Implementations are single-threaded; callers must synchronise externally.
class MemoryAllocator {
public:
    virtual ~MemoryAllocator() = default;

    virtual Result<AllocationHandle> allocate(
        std::size_t bytes, std::size_t alignment = alignof(std::max_align_t)) = 0;

    virtual Status deallocate(std::uint64_t allocation_id) = 0;

    [[nodiscard]] virtual AllocatorStats stats() const = 0;

    /// Checks every structural invariant. Returns a description of the first
    /// violation, or nullopt when the state is consistent. O(n log n).
    [[nodiscard]] virtual std::optional<std::string> diagnose() const = 0;

    [[nodiscard]] bool validate() const { return !diagnose().has_value(); }

    [[nodiscard]] virtual AllocationStrategy strategy() const noexcept = 0;
    [[nodiscard]] virtual std::size_t capacity() const noexcept = 0;

    /// Writable view of a live allocation's requested bytes (empty if `id` is not live).
    [[nodiscard]] virtual std::span<std::byte> memory(std::uint64_t allocation_id) noexcept = 0;
};

}  // namespace memoryforge
