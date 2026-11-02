#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "memoryforge/common/result.hpp"

namespace memoryforge {

/// One step of an allocation trace. A trace is independent of any allocator:
/// a Free names the *ordinal* of the Allocate it releases (the n-th Allocate
/// in the trace, 0-based), not an allocator-issued id. That lets the same
/// trace be replayed against every strategy even when some requests fail.
struct AllocOp {
    enum class Kind : std::uint8_t { Allocate, Free };
    Kind kind = Kind::Allocate;
    std::uint64_t ordinal = 0;  ///< Free: ordinal of the allocation to release.
    std::size_t size = 0;       ///< Allocate only.
    std::size_t alignment = 0;  ///< Allocate only.

    bool operator==(const AllocOp&) const = default;
};

struct AllocationTrace {
    std::vector<AllocOp> ops;

    [[nodiscard]] std::size_t allocate_count() const noexcept;
    /// FNV-1a over every field of every op; recorded in reports to prove two runs used the same trace.
    [[nodiscard]] std::uint64_t checksum() const noexcept;
};

/// Checks that every Free refers to an earlier Allocate, at most once.
Status validate_trace(const AllocationTrace& trace);

/// Text format, one op per line:  "A <size> <alignment>"  or  "F <ordinal>".
/// Blank lines and lines starting with '#' are ignored.
Result<AllocationTrace> parse_allocation_trace(std::string_view text);
std::string format_allocation_trace(const AllocationTrace& trace);

}  // namespace memoryforge
