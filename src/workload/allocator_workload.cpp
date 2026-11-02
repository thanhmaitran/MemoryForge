#include "memoryforge/workload/allocator_workload.hpp"

#include <algorithm>
#include <cmath>

#include "memoryforge/common/io.hpp"
#include "memoryforge/common/rng.hpp"

namespace memoryforge {
namespace {

constexpr std::size_t kGranularity = 16;  // must match the allocator's block granularity
constexpr std::uint64_t kStressPhaseLength = 1000;

constexpr std::uint64_t round_up16(std::uint64_t v) { return (v + kGranularity - 1) & ~std::uint64_t{kGranularity - 1}; }

class Generator {
public:
    explicit Generator(const AllocatorWorkloadConfig& c, AllocatorWorkloadKind kind)
        : c_(c),
          kind_(kind),
          rng_(c.seed),
          cap_bytes_(static_cast<std::uint64_t>(static_cast<double>(c.heap_size_bytes) * c.target_utilization)) {}

    AllocationTrace run() {
        trace_.ops.reserve(static_cast<std::size_t>(c_.operations));
        if (kind_ == AllocatorWorkloadKind::BurstAllocation) {
            run_bursts();
        } else {
            run_stepwise();
        }
        return std::move(trace_);
    }

private:
    struct Live {
        std::uint64_t ordinal;
        std::uint64_t rounded_size;
        bool pinned;
    };

    [[nodiscard]] bool done() const { return trace_.ops.size() >= c_.operations; }

    static std::uint64_t floor_log2(std::uint64_t v) {
        std::uint64_t e = 0;
        while ((v >> (e + 1)) != 0) ++e;
        return e;
    }
    static std::uint64_t ceil_log2(std::uint64_t v) {
        std::uint64_t e = 0;
        while ((std::uint64_t{1} << e) < v) ++e;
        return e;
    }

    std::uint64_t draw_size() {
        const std::uint64_t lo = c_.min_allocation_bytes;
        const std::uint64_t hi = c_.max_allocation_bytes;
        switch (kind_) {
            case AllocatorWorkloadKind::SizeSkew: {
                if (rng_.chance(c_.large_request_probability)) {
                    return rng_.uniform(std::max(lo, hi / 2), hi);
                }
                return rng_.uniform(lo, lo + (hi - lo) / 16);
            }
            case AllocatorWorkloadKind::LongRunningStress: {
                const std::uint64_t lo_exp = ceil_log2(lo);
                const std::uint64_t hi_exp = floor_log2(hi);
                if (lo_exp <= hi_exp && rng_.chance(0.5)) {
                    return std::uint64_t{1} << rng_.uniform(lo_exp, hi_exp);
                }
                return rng_.uniform(lo, hi);
            }
            default: return rng_.uniform(lo, hi);
        }
    }

    std::uint64_t draw_alignment() {
        if (c_.alignment_policy == AlignmentPolicy::PowerOfTwo) return std::uint64_t{16} << rng_.uniform(0, 4);
        return 16;
    }

    void emit_alloc(std::uint64_t size, bool pinned) {
        const std::uint64_t ordinal = next_ordinal_++;
        trace_.ops.push_back({AllocOp::Kind::Allocate, ordinal, static_cast<std::size_t>(size),
                              static_cast<std::size_t>(draw_alignment())});
        live_.push_back({ordinal, round_up16(size), pinned});
        live_bytes_ += round_up16(size);
    }

    std::size_t pick_index(bool avoid_pinned) {
        const std::size_t n = live_.size();
        std::size_t idx = 0;
        switch (c_.release_policy) {
            case ReleasePolicy::Uniform: idx = static_cast<std::size_t>(rng_.uniform(0, n - 1)); break;
            case ReleasePolicy::AgeWeighted: {
                const double u = rng_.uniform01();
                idx = std::min(n - 1, static_cast<std::size_t>(static_cast<double>(n) * u * u));
                break;
            }
            case ReleasePolicy::Lifo: idx = n - 1; break;
            case ReleasePolicy::Fifo: idx = 0; break;
        }
        if (avoid_pinned) {
            for (std::size_t step = 0; step < n; ++step) {
                const std::size_t j = (idx + step) % n;
                if (!live_[j].pinned) return j;
            }
        }
        return idx;
    }

    void emit_free_at(std::size_t idx) {
        const Live victim = live_[idx];
        live_.erase(live_.begin() + static_cast<std::ptrdiff_t>(idx));
        live_bytes_ -= victim.rounded_size;
        trace_.ops.push_back({AllocOp::Kind::Free, victim.ordinal, 0, 0});
    }

    [[nodiscard]] double current_alloc_probability() const {
        if (kind_ != AllocatorWorkloadKind::LongRunningStress) return c_.allocation_probability;
        const bool heavy = ((trace_.ops.size() / kStressPhaseLength) % 2) == 0;
        return heavy ? std::min(0.95, c_.allocation_probability + 0.15)
                     : std::max(0.05, c_.allocation_probability - 0.25);
    }

    void run_stepwise() {
        while (!done()) {
            const std::uint64_t size = draw_size();
            const bool want_alloc = live_.empty() || rng_.chance(current_alloc_probability());
            const bool over_cap = live_bytes_ + round_up16(size) > cap_bytes_;
            if (want_alloc && (!over_cap || live_.empty())) {
                const bool pinned =
                    kind_ == AllocatorWorkloadKind::MixedLifetimes && rng_.chance(c_.long_lived_fraction);
                emit_alloc(size, pinned);
            } else {
                // A forced release (memory pressure) may take any block, even long-lived ones.
                const bool forced = want_alloc && over_cap;
                emit_free_at(pick_index(kind_ == AllocatorWorkloadKind::MixedLifetimes && !forced));
            }
        }
    }

    void run_bursts() {
        while (!done()) {
            for (std::uint64_t i = 0; i < c_.burst_size && !done(); ++i) {
                const std::uint64_t size = draw_size();
                if (live_bytes_ + round_up16(size) > cap_bytes_ && !live_.empty()) break;
                emit_alloc(size, false);
            }
            auto to_free = static_cast<std::size_t>(static_cast<double>(live_.size()) * c_.burst_release_fraction);
            if (to_free == 0 && !live_.empty()) to_free = 1;
            for (std::size_t i = 0; i < to_free && !done() && !live_.empty(); ++i) {
                emit_free_at(pick_index(false));
            }
        }
    }

    const AllocatorWorkloadConfig& c_;
    AllocatorWorkloadKind kind_;
    Rng rng_;
    std::uint64_t cap_bytes_;
    AllocationTrace trace_;
    std::vector<Live> live_;
    std::uint64_t live_bytes_ = 0;
    std::uint64_t next_ordinal_ = 0;
};

bool in_unit_interval(double v) { return v >= 0.0 && v <= 1.0; }  // false for NaN

}  // namespace

std::optional<AllocatorWorkloadKind> workload_kind_from_id(std::string_view id) noexcept {
    if (id == "W01") return AllocatorWorkloadKind::UniformRandom;
    if (id == "W02") return AllocatorWorkloadKind::MixedLifetimes;
    if (id == "W03") return AllocatorWorkloadKind::BurstAllocation;
    if (id == "W04") return AllocatorWorkloadKind::SizeSkew;
    if (id == "W05") return AllocatorWorkloadKind::LongRunningStress;
    return std::nullopt;
}

std::string_view to_string(ReleasePolicy p) noexcept {
    switch (p) {
        case ReleasePolicy::Uniform: return "uniform";
        case ReleasePolicy::AgeWeighted: return "age_weighted";
        case ReleasePolicy::Lifo: return "lifo";
        case ReleasePolicy::Fifo: return "fifo";
    }
    return "unknown";
}

std::string_view to_string(AlignmentPolicy p) noexcept {
    return p == AlignmentPolicy::PowerOfTwo ? "power_of_two" : "default";
}

Status validate_config(const AllocatorWorkloadConfig& c) {
    auto bad = [](const std::string& why) { return Status(make_error(ErrorCode::InvalidConfig, why)); };
    if (c.schema_version != 1) return bad("unsupported schema_version (expected 1)");
    if (!workload_kind_from_id(c.workload_id)) return bad("workload_id must be one of W01..W05");
    if (c.heap_size_bytes == 0 || c.heap_size_bytes % kGranularity != 0) return bad("heap_size_bytes must be a non-zero multiple of 16");
    if (c.heap_size_bytes > (std::uint64_t{1} << 32)) return bad("heap_size_bytes above 4 GiB is not supported");
    if (c.operations == 0) return bad("operations must be at least 1");
    if (c.min_allocation_bytes == 0) return bad("min_allocation_bytes must be at least 1");
    if (c.max_allocation_bytes < c.min_allocation_bytes) return bad("max_allocation_bytes must be >= min_allocation_bytes");
    if (c.max_allocation_bytes > c.heap_size_bytes) return bad("max_allocation_bytes must not exceed heap_size_bytes");
    if (!in_unit_interval(c.allocation_probability)) return bad("allocation_probability must be in [0, 1]");
    if (!(c.target_utilization > 0.0 && c.target_utilization <= 1.0)) return bad("target_utilization must be in (0, 1]");
    if (!in_unit_interval(c.long_lived_fraction)) return bad("long_lived_fraction must be in [0, 1]");
    if (!in_unit_interval(c.burst_release_fraction)) return bad("burst_release_fraction must be in [0, 1]");
    if (!in_unit_interval(c.large_request_probability)) return bad("large_request_probability must be in [0, 1]");
    if (c.burst_size == 0) return bad("burst_size must be at least 1");
    if (c.warmup_operations >= c.operations) return bad("warmup_operations must be smaller than operations");
    if (c.repetitions == 0) return bad("repetitions must be at least 1");
    return Status::success();
}

Result<AllocatorWorkloadConfig> parse_allocator_config(const json::Value& root) {
    AllocatorWorkloadConfig c;
    std::string release = std::string(to_string(c.release_policy));
    std::string alignment = std::string(to_string(c.alignment_policy));
    json::ObjectReader reader(root, "allocator workload config");
    reader.read("schema_version", c.schema_version)
        .read("workload_id", c.workload_id)
        .read("name", c.name)
        .read("seed", c.seed)
        .read("heap_size_bytes", c.heap_size_bytes)
        .read("operations", c.operations)
        .read("min_allocation_bytes", c.min_allocation_bytes)
        .read("max_allocation_bytes", c.max_allocation_bytes)
        .read("allocation_probability", c.allocation_probability)
        .read("release_policy", release)
        .read("alignment_policy", alignment)
        .read("warmup_operations", c.warmup_operations)
        .read("target_utilization", c.target_utilization)
        .read("long_lived_fraction", c.long_lived_fraction)
        .read("burst_size", c.burst_size)
        .read("burst_release_fraction", c.burst_release_fraction)
        .read("large_request_probability", c.large_request_probability)
        .read("repetitions", c.repetitions)
        .read("sample_interval", c.sample_interval);
    if (Status s = reader.finish(); !s) return s.error();

    bool matched = false;
    for (const ReleasePolicy p : {ReleasePolicy::Uniform, ReleasePolicy::AgeWeighted, ReleasePolicy::Lifo, ReleasePolicy::Fifo}) {
        if (release == to_string(p)) {
            c.release_policy = p;
            matched = true;
        }
    }
    if (!matched) return make_error(ErrorCode::InvalidConfig, "release_policy must be uniform, age_weighted, lifo or fifo");
    if (alignment == "default") {
        c.alignment_policy = AlignmentPolicy::Default;
    } else if (alignment == "power_of_two") {
        c.alignment_policy = AlignmentPolicy::PowerOfTwo;
    } else {
        return make_error(ErrorCode::InvalidConfig, "alignment_policy must be default or power_of_two");
    }
    if (Status s = validate_config(c); !s) return s.error();
    return c;
}

Result<AllocatorWorkloadConfig> load_allocator_config(const std::filesystem::path& path) {
    auto text = read_text_file(path);
    if (!text) return text.error();
    auto doc = json::parse(text.value());
    if (!doc) return make_error(doc.error().code, path.string() + ": " + doc.error().message);
    auto config = parse_allocator_config(doc.value());
    if (!config) return make_error(config.error().code, path.string() + ": " + config.error().message);
    return config;
}

void write_config_json(json::Writer& w, const AllocatorWorkloadConfig& c) {
    w.begin_object();
    w.key("schema_version"); w.value(c.schema_version);
    w.key("workload_id"); w.value(c.workload_id);
    w.key("name"); w.value(c.name);
    w.key("seed"); w.value(c.seed);
    w.key("heap_size_bytes"); w.value(c.heap_size_bytes);
    w.key("operations"); w.value(c.operations);
    w.key("min_allocation_bytes"); w.value(c.min_allocation_bytes);
    w.key("max_allocation_bytes"); w.value(c.max_allocation_bytes);
    w.key("allocation_probability"); w.value(c.allocation_probability);
    w.key("release_policy"); w.value(to_string(c.release_policy));
    w.key("alignment_policy"); w.value(to_string(c.alignment_policy));
    w.key("warmup_operations"); w.value(c.warmup_operations);
    w.key("target_utilization"); w.value(c.target_utilization);
    w.key("long_lived_fraction"); w.value(c.long_lived_fraction);
    w.key("burst_size"); w.value(c.burst_size);
    w.key("burst_release_fraction"); w.value(c.burst_release_fraction);
    w.key("large_request_probability"); w.value(c.large_request_probability);
    w.key("repetitions"); w.value(c.repetitions);
    w.key("sample_interval"); w.value(c.sample_interval);
    w.end_object();
}

Result<AllocationTrace> generate_allocation_trace(const AllocatorWorkloadConfig& config) {
    if (Status s = validate_config(config); !s) return s.error();
    const auto kind = workload_kind_from_id(config.workload_id);
    return Generator(config, *kind).run();
}

}  // namespace memoryforge
