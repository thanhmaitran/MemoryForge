#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "memoryforge/common/result.hpp"
#include "memoryforge/vm/replacement_policy.hpp"
#include "memoryforge/vm/types.hpp"

namespace memoryforge {

/// Logical virtual-memory simulator: a flat page table, a fixed number of
/// physical frames and a pluggable replacement policy. It models the
/// architecture (VA -> VPN + offset -> PFN + offset -> PA, faults, eviction,
/// write-back counting) and does not interact with the host OS.
///
///   VPN    = VA / page_size            offset = VA % page_size
///   PA     = PFN * page_size + offset
///
/// Free frames are handed out lowest-first and nothing is replaced while a
/// free frame exists.
class VirtualMemory {
public:
    static Result<VirtualMemory> create(VmConfig config, std::unique_ptr<ReplacementPolicy> policy);

    /// Performs one memory access. Out-of-range addresses return OutOfRange
    /// and leave all state untouched.
    Result<TranslationResult> access(std::uint64_t virtual_address, AccessType type);

    /// Translates without side effects. nullopt if the page is not resident or the address is out of range.
    [[nodiscard]] std::optional<std::uint64_t> translate(std::uint64_t virtual_address) const;

    [[nodiscard]] const VmConfig& config() const noexcept { return config_; }
    [[nodiscard]] const VmStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const ReplacementPolicy& policy() const noexcept { return *policy_; }
    [[nodiscard]] std::uint64_t resident_pages() const noexcept;
    /// Bytes of simulator bookkeeping (page table, frame owners, history bits).
    [[nodiscard]] std::uint64_t metadata_bytes() const noexcept;

    /// Checks every invariant; returns the first violation or nullopt.
    [[nodiscard]] std::optional<std::string> diagnose() const;
    [[nodiscard]] bool validate() const { return !diagnose().has_value(); }

private:
    VirtualMemory(VmConfig config, std::unique_ptr<ReplacementPolicy> policy);

    static constexpr Vpn kNoPage = ~Vpn{0};

    VmConfig config_;
    std::unique_ptr<ReplacementPolicy> policy_;
    std::vector<PageTableEntry> page_table_;
    std::vector<Vpn> frame_owner_;     // frame -> VPN, kNoPage when free
    std::vector<Pfn> free_frames_;     // stack; back() is the lowest free frame
    std::vector<bool> ever_loaded_;    // compulsory-fault tracking
    VmStats stats_;
};

}  // namespace memoryforge
