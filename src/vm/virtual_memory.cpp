#include "memoryforge/vm/virtual_memory.hpp"

#include <string>

namespace memoryforge {
namespace {
constexpr bool is_power_of_two(std::uint64_t v) noexcept { return v != 0 && (v & (v - 1)) == 0; }
}  // namespace

VirtualMemory::VirtualMemory(VmConfig config, std::unique_ptr<ReplacementPolicy> policy)
    : config_(config),
      policy_(std::move(policy)),
      page_table_(static_cast<std::size_t>(config.virtual_pages)),
      frame_owner_(static_cast<std::size_t>(config.frames), kNoPage),
      ever_loaded_(static_cast<std::size_t>(config.virtual_pages), false) {
    free_frames_.reserve(static_cast<std::size_t>(config.frames));
    for (std::uint64_t f = config.frames; f-- > 0;) free_frames_.push_back(f);
}

Result<VirtualMemory> VirtualMemory::create(VmConfig config,
                                            std::unique_ptr<ReplacementPolicy> policy) {
    if (!is_power_of_two(config.page_size)) {
        return make_error(ErrorCode::InvalidConfig, "page_size must be a power of two");
    }
    if (config.virtual_pages == 0 || config.frames == 0) {
        return make_error(ErrorCode::InvalidConfig, "virtual_pages and frames must be non-zero");
    }
    if (config.virtual_pages > (1ULL << 28)) {
        return make_error(ErrorCode::InvalidConfig, "virtual_pages is unreasonably large");
    }
    if (config.page_size > (~std::uint64_t{0}) / config.virtual_pages) {
        return make_error(ErrorCode::InvalidConfig, "address space size overflows 64 bits");
    }
    if (!policy) return make_error(ErrorCode::InvalidArgument, "a replacement policy is required");
    return VirtualMemory(config, std::move(policy));
}

Result<TranslationResult> VirtualMemory::access(std::uint64_t virtual_address, AccessType type) {
    if (virtual_address >= config_.page_size * config_.virtual_pages) {
        return make_error(ErrorCode::OutOfRange, "virtual address " + std::to_string(virtual_address) +
                                                     " is outside the address space");
    }
    TranslationResult result;
    result.virtual_address = virtual_address;
    result.vpn = virtual_address / config_.page_size;
    result.offset = virtual_address % config_.page_size;
    const bool is_write = (type == AccessType::Write);
    PageTableEntry& entry = page_table_[static_cast<std::size_t>(result.vpn)];

    policy_->begin_access(stats_.accesses, result.vpn);
    ++stats_.accesses;
    ++(is_write ? stats_.writes : stats_.reads);

    if (entry.present) {
        ++stats_.hits;
        policy_->on_hit(result.vpn);
        if (is_write) entry.dirty = true;
        result.pfn = entry.frame;
    } else {
        ++stats_.page_faults;
        result.page_fault = true;
        const auto vpn_index = static_cast<std::size_t>(result.vpn);
        if (!ever_loaded_[vpn_index]) {
            result.compulsory = true;
            ++stats_.compulsory_faults;
            ever_loaded_[vpn_index] = true;
        }

        Pfn frame = 0;
        if (!free_frames_.empty()) {
            frame = free_frames_.back();
            free_frames_.pop_back();
        } else {
            const Vpn victim = policy_->select_victim();
            if (victim >= config_.virtual_pages ||
                !page_table_[static_cast<std::size_t>(victim)].present) {
                return make_error(ErrorCode::ValidationFailed,
                                  "replacement policy '" + std::string(policy_->name()) +
                                      "' selected a page that is not resident");
            }
            PageTableEntry& old = page_table_[static_cast<std::size_t>(victim)];
            frame = old.frame;
            if (old.dirty) {
                ++stats_.writebacks;
                result.wrote_back = true;
            }
            old = PageTableEntry{};
            policy_->on_evict(victim);
            ++stats_.replacements;
            result.evicted = victim;
        }
        entry.present = true;
        entry.dirty = is_write;
        entry.frame = frame;
        frame_owner_[static_cast<std::size_t>(frame)] = result.vpn;
        policy_->on_load(result.vpn);
        result.pfn = frame;
    }
    result.physical_address = result.pfn * config_.page_size + result.offset;
    return result;
}

std::optional<std::uint64_t> VirtualMemory::translate(std::uint64_t virtual_address) const {
    if (virtual_address >= config_.page_size * config_.virtual_pages) return std::nullopt;
    const Vpn vpn = virtual_address / config_.page_size;
    const PageTableEntry& entry = page_table_[static_cast<std::size_t>(vpn)];
    if (!entry.present) return std::nullopt;
    return entry.frame * config_.page_size + virtual_address % config_.page_size;
}

std::uint64_t VirtualMemory::resident_pages() const noexcept {
    return config_.frames - free_frames_.size();
}

std::uint64_t VirtualMemory::metadata_bytes() const noexcept {
    return page_table_.size() * sizeof(PageTableEntry) + frame_owner_.size() * sizeof(Vpn) +
           free_frames_.capacity() * sizeof(Pfn) + (ever_loaded_.size() + 7) / 8;
}

std::optional<std::string> VirtualMemory::diagnose() const {
    std::uint64_t present = 0;
    for (std::size_t vpn = 0; vpn < page_table_.size(); ++vpn) {
        const PageTableEntry& e = page_table_[vpn];
        if (!e.present) {
            if (e.dirty) return "page " + std::to_string(vpn) + " is dirty but not resident";
            continue;
        }
        ++present;
        if (e.frame >= config_.frames) return "page " + std::to_string(vpn) + " maps outside physical memory";
        if (frame_owner_[static_cast<std::size_t>(e.frame)] != vpn) {
            return "frame " + std::to_string(e.frame) + " is not owned by page " + std::to_string(vpn) +
                   " (two pages share a frame or ownership is stale)";
        }
    }
    std::uint64_t owned = 0;
    for (std::size_t f = 0; f < frame_owner_.size(); ++f) {
        const Vpn owner = frame_owner_[f];
        if (owner == kNoPage) continue;
        ++owned;
        if (owner >= config_.virtual_pages || !page_table_[static_cast<std::size_t>(owner)].present ||
            page_table_[static_cast<std::size_t>(owner)].frame != f) {
            return "frame " + std::to_string(f) + " has an owner that does not map back to it";
        }
    }
    if (owned != present) return "resident page count differs from owned frame count";
    if (owned + free_frames_.size() != config_.frames) return "used + free frames do not equal total frames";
    if (stats_.hits + stats_.page_faults != stats_.accesses) return "hits + faults != accesses";
    if (stats_.reads + stats_.writes != stats_.accesses) return "reads + writes != accesses";
    if (stats_.compulsory_faults > stats_.page_faults) return "compulsory faults exceed total faults";
    // Every fault either fills a free frame or replaces a page, so replacements
    // happen only once memory is full.
    if (stats_.page_faults != stats_.replacements + owned) {
        return "faults != replacements + frames in use (a page was replaced while a frame was free)";
    }
    if (stats_.writebacks > stats_.replacements) return "more write-backs than replacements";
    return policy_->diagnose();
}

}  // namespace memoryforge
