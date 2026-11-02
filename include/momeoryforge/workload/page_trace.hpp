#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memoryforge/common/json.hpp"
#include "memoryforge/common/result.hpp"
#include "memoryforge/vm/types.hpp"

namespace memoryforge {

struct PageAccess {
    Vpn vpn = 0;
    bool write = false;
    bool operator==(const PageAccess&) const = default;
};

struct PageTrace {
    std::vector<PageAccess> accesses;

    [[nodiscard]] std::vector<Vpn> vpns() const;
    [[nodiscard]] std::uint64_t checksum() const noexcept;
};

/// Virtual address used for the i-th access. The offset is a deterministic
/// function of i so that offset handling is exercised without extra randomness.
[[nodiscard]] std::uint64_t virtual_address_for(const PageAccess& access, std::uint64_t index,
                                                std::uint64_t page_size) noexcept;

/// Text format: page numbers separated by commas and/or whitespace; a trailing
/// 'w' marks a write (e.g. "3w"). Lines starting with '#' are comments.
Result<PageTrace> parse_page_trace(std::string_view text);
std::string format_page_trace(const PageTrace& trace);

enum class PagePattern { Sequential, RepeatedLoop, WorkingSetShift, Random, Adversarial };

std::string_view to_string(PagePattern p) noexcept;
std::optional<PagePattern> parse_pattern(std::string_view name) noexcept;

/// Effective configuration of one virtual-memory workload (JSON schema version 1).
struct VmWorkloadConfig {
    std::uint64_t schema_version = 1;
    std::string workload_id = "V01";
    std::string name = "unnamed";
    std::uint64_t seed = 1;
    PagePattern pattern = PagePattern::Random;
    std::uint64_t page_size = 4096;
    std::uint64_t virtual_pages = 256;
    std::vector<std::uint64_t> frames = {4, 8, 16};  ///< Frame counts to sweep.
    std::uint64_t accesses = 10000;
    double write_probability = 0.3;

    std::uint64_t loop_pages = 8;          ///< repeated_loop: size of the cycled page set.
    std::uint64_t working_set_pages = 8;   ///< working_set_shift: pages per working set.
    std::uint64_t phase_length = 1000;     ///< working_set_shift: accesses before the set moves.
    std::uint64_t adversarial_frames = 0;  ///< adversarial: cycle over this+1 pages (0 = smallest `frames`).
    std::uint64_t repetitions = 5;         ///< Timed repetitions per (policy, frames).
};

Status validate_config(const VmWorkloadConfig& config);
Result<VmWorkloadConfig> parse_vm_config(const json::Value& root);
Result<VmWorkloadConfig> load_vm_config(const std::filesystem::path& path);
void write_config_json(json::Writer& w, const VmWorkloadConfig& config);

Result<PageTrace> generate_page_trace(const VmWorkloadConfig& config);

}  // namespace memoryforge
