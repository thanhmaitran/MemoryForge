#pragma once

#include <cstdint>
#include <optional>

namespace memoryforge {

using Vpn = std::uint64_t;  ///< Virtual page number.
using Pfn = std::uint64_t;  ///< Physical frame number.

enum class AccessType { Read, Write };

/// Geometry of the simulated machine.
struct VmConfig {
    std::uint64_t page_size = 4096;      ///< Bytes per page; must be a power of two.
    std::uint64_t virtual_pages = 1024;  ///< Size of the virtual address space in pages.
    std::uint64_t frames = 8;            ///< Physical frames available.
};

struct PageTableEntry {
    bool present = false;
    bool dirty = false;
    Pfn frame = 0;  ///< Meaningful only while `present`.
};

struct TranslationResult {
    std::uint64_t virtual_address = 0;
    Vpn vpn = 0;
    std::uint64_t offset = 0;
    Pfn pfn = 0;
    std::uint64_t physical_address = 0;
    bool page_fault = false;
    bool compulsory = false;        ///< First-ever reference to this page.
    std::optional<Vpn> evicted;     ///< Page replaced to serve this fault, if any.
    bool wrote_back = false;        ///< The evicted page was dirty.
};

struct VmStats {
    std::uint64_t accesses = 0;
    std::uint64_t reads = 0;
    std::uint64_t writes = 0;
    std::uint64_t hits = 0;
    std::uint64_t page_faults = 0;
    std::uint64_t compulsory_faults = 0;
    std::uint64_t replacements = 0;
    std::uint64_t writebacks = 0;

    [[nodiscard]] double fault_rate() const noexcept {
        return accesses == 0 ? 0.0
                             : static_cast<double>(page_faults) / static_cast<double>(accesses);
    }
};

}  // namespace memoryforge
