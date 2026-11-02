#pragma once

#include <map>
#include <memory>
#include <unordered_map>

#include "memoryforge/allocator/allocator.hpp"

namespace memoryforge {

/// Arena allocator with an address-ordered free list and out-of-band metadata.
///
/// Layout convention shared by all strategies (so comparisons are fair):
///  - The arena is one contiguous buffer aligned to kMaxAlignment.
///  - Every block offset and size is a multiple of kGranularity (16 bytes).
///  - A request of n bytes reserves align_up(n, 16) bytes; there is no
///    per-block header, so internal overhead is rounding only.
///  - Alignment is applied to the block offset (power of two, 1..kMaxAlignment,
///    raised to at least 16). Any gap in front of an aligned block stays free.
///  - Splitting: leftovers on both sides of the chosen block return to the free
///    list; there is no minimum split size because leftovers are >= 16 bytes.
///  - Coalescing: a freed block merges immediately with free neighbours.
///
/// The strategies differ only in which free block they pick:
///  First Fit = lowest address that fits; Best Fit = smallest block that fits;
///  Worst Fit = largest block that fits; ties go to the lowest address.
class FreeListAllocator final : public MemoryAllocator {
public:
    static constexpr std::size_t kGranularity = 16;
    static constexpr std::size_t kMaxAlignment = 4096;

    /// `capacity_bytes` must be a non-zero multiple of kGranularity.
    static Result<std::unique_ptr<FreeListAllocator>> create(std::size_t capacity_bytes,
                                                             AllocationStrategy strategy);

    Result<AllocationHandle> allocate(std::size_t bytes, std::size_t alignment) override;
    Status deallocate(std::uint64_t allocation_id) override;
    [[nodiscard]] AllocatorStats stats() const override;
    [[nodiscard]] std::optional<std::string> diagnose() const override;
    [[nodiscard]] AllocationStrategy strategy() const noexcept override { return strategy_; }
    [[nodiscard]] std::size_t capacity() const noexcept override { return capacity_; }
    [[nodiscard]] std::span<std::byte> memory(std::uint64_t allocation_id) noexcept override;

private:
    struct ArenaDeleter {
        void operator()(std::byte* p) const noexcept {
            ::operator delete(p, std::align_val_t{kMaxAlignment});
        }
    };
    struct LiveBlock {
        std::size_t offset;
        std::size_t requested;
        std::size_t allocated;
        std::size_t alignment;
    };

    FreeListAllocator(std::size_t capacity, AllocationStrategy strategy,
                      std::unique_ptr<std::byte[], ArenaDeleter> arena);

    std::size_t capacity_;
    AllocationStrategy strategy_;
    std::unique_ptr<std::byte[], ArenaDeleter> arena_;

    std::map<std::size_t, std::size_t> free_blocks_;  // offset -> size
    std::unordered_map<std::uint64_t, LiveBlock> live_;
    std::uint64_t next_id_ = 1;

    std::size_t live_requested_ = 0;
    std::size_t live_allocated_ = 0;
    std::size_t peak_live_allocated_ = 0;
    std::uint64_t allocation_count_ = 0;
    std::uint64_t deallocation_count_ = 0;
    std::uint64_t failed_allocations_ = 0;
    std::uint64_t search_steps_ = 0;
};

/// Convenience factory returning the abstract interface.
Result<std::unique_ptr<MemoryAllocator>> make_allocator(AllocationStrategy strategy,
                                                        std::size_t capacity_bytes);

}  // namespace memoryforge
