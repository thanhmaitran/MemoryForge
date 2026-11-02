#include "memoryforge/metrics/latency.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace memoryforge {
namespace {

double nearest_rank(const std::vector<std::uint64_t>& sorted, double percent) {
    const auto n = static_cast<double>(sorted.size());
    auto rank = static_cast<std::size_t>(std::ceil(percent / 100.0 * n));
    rank = std::clamp<std::size_t>(rank, 1, sorted.size());
    return static_cast<double>(sorted[rank - 1]);
}

}  // namespace

LatencySummary summarize_latency(std::vector<std::uint64_t> samples_ns) {
    LatencySummary s;
    s.count = samples_ns.size();
    if (samples_ns.empty()) return s;
    std::sort(samples_ns.begin(), samples_ns.end());
    s.min = static_cast<double>(samples_ns.front());
    s.max = static_cast<double>(samples_ns.back());
    long double sum = 0.0L;
    for (const auto v : samples_ns) sum += static_cast<long double>(v);
    s.mean = static_cast<double>(sum / static_cast<long double>(samples_ns.size()));
    s.median = nearest_rank(samples_ns, 50.0);
    if (samples_ns.size() >= 20) s.p95 = nearest_rank(samples_ns, 95.0);
    if (samples_ns.size() >= 100) s.p99 = nearest_rank(samples_ns, 99.0);
    return s;
}

double median_of(std::vector<double> values) {
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    if (values.size() % 2 == 1) return values[mid];
    return (values[mid - 1] + values[mid]) / 2.0;
}

double measure_timer_overhead_ns(std::size_t iterations) {
    std::vector<std::uint64_t> deltas;
    deltas.reserve(iterations);
    for (std::size_t i = 0; i < iterations; ++i) {
        const std::uint64_t a = now_ns();
        const std::uint64_t b = now_ns();
        deltas.push_back(b - a);
    }
    std::sort(deltas.begin(), deltas.end());
    return deltas.empty() ? 0.0 : static_cast<double>(deltas[deltas.size() / 2]);
}

}  // namespace memoryforge
