#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "memoryforge/common/result.hpp"
#include "memoryforge/vm/types.hpp"

namespace memoryforge {

enum class ReplacementPolicyKind { Fifo, Lru, Optimal, Clock };

constexpr std::string_view to_string(ReplacementPolicyKind k) noexcept {
    switch (k) {
        case ReplacementPolicyKind::Fifo: return "fifo";
        case ReplacementPolicyKind::Lru: return "lru";
        case ReplacementPolicyKind::Optimal: return "opt";
        case ReplacementPolicyKind::Clock: return "clock";
    }
    return "unknown";
}

constexpr std::optional<ReplacementPolicyKind> parse_policy(std::string_view name) noexcept {
    if (name == "fifo") return ReplacementPolicyKind::Fifo;
    if (name == "lru") return ReplacementPolicyKind::Lru;
    if (name == "opt") return ReplacementPolicyKind::Optimal;
    if (name == "clock") return ReplacementPolicyKind::Clock;
    return std::nullopt;
}

inline constexpr ReplacementPolicyKind kAllPolicies[] = {
    ReplacementPolicyKind::Fifo, ReplacementPolicyKind::Lru, ReplacementPolicyKind::Optimal,
    ReplacementPolicyKind::Clock};

/// Page replacement policy driven by the VirtualMemory simulator.
///
/// Call order for one access: begin_access(); then either on_hit() for a
/// resident page, or (on a fault with no free frame) select_victim() followed
/// by on_evict(victim), and finally on_load() for the page brought in.
/// Online policies (FIFO, LRU, Clock) only see the past. Only OptimalPolicy is
/// constructed with the future trace.
class ReplacementPolicy {
public:
    virtual ~ReplacementPolicy() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    virtual void begin_access(std::uint64_t /*step*/, Vpn /*vpn*/) {}
    virtual void on_hit(Vpn vpn) = 0;
    virtual void on_load(Vpn vpn) = 0;
    [[nodiscard]] virtual Vpn select_victim() = 0;
    virtual void on_evict(Vpn vpn) = 0;

    /// Internal consistency check; nullopt when healthy.
    [[nodiscard]] virtual std::optional<std::string> diagnose() const { return std::nullopt; }
};

/// `future_trace` is required for (and only used by) the optimal policy. It
/// must be exactly the sequence of VPNs the simulator will be driven with.
Result<std::unique_ptr<ReplacementPolicy>> make_policy(ReplacementPolicyKind kind,
                                                       std::uint64_t frames,
                                                       std::span<const Vpn> future_trace = {});

}  // namespace memoryforge
