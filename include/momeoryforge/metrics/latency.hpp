#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace memoryforge {

/// Monotonic nanosecond clock used by all measurements.
[[nodiscard]] inline std::uint64_t now_ns() noexcept {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch())
            .count());
}

/// Summary statistics over latency samples, in nanoseconds.
/// Percentiles use the nearest-rank method. A tail percentile is NaN when
/// there are too few samples to support it (p95 needs >= 20, p99 needs >= 100).
struct LatencySummary {
    std::size_t count = 0;
    double min = std::numeric_limits<double>::quiet_NaN();
    double mean = std::numeric_limits<double>::quiet_NaN();
    double median = std::numeric_limits<double>::quiet_NaN();
    double p95 = std::numeric_limits<double>::quiet_NaN();
    double p99 = std::numeric_limits<double>::quiet_NaN();
    double max = std::numeric_limits<double>::quiet_NaN();
};

/// Takes the samples by value because it sorts them.
[[nodiscard]] LatencySummary summarize_latency(std::vector<std::uint64_t> samples_ns);

/// Median of arbitrary doubles (NaN for an empty input).
[[nodiscard]] double median_of(std::vector<double> values);

/// Cost of one now_ns() call pair, measured by timing back-to-back calls.
/// Latency samples include this overhead; it is reported, not subtracted.
[[nodiscard]] double measure_timer_overhead_ns(std::size_t iterations = 20000);

}  // namespace memoryforge
