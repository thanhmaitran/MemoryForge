#pragma once

#include <cstdint>

namespace memoryforge {

/// Deterministic xoshiro256** generator seeded through SplitMix64.
///
/// The standard <random> distributions are not specified bit-for-bit, so the
/// same seed can produce different traces on different standard libraries.
/// MemoryForge implements its own bounded sampling so a seed always yields the
/// same workload on every compiler and platform.
class Rng {
public:
    explicit Rng(std::uint64_t seed) noexcept {
        std::uint64_t x = seed;
        for (auto& word : state_) {
            x += 0x9E3779B97F4A7C15ULL;
            std::uint64_t z = x;
            z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
            word = z ^ (z >> 31U);
        }
    }

    std::uint64_t next_u64() noexcept {
        const std::uint64_t result = rotl(state_[1] * 5U, 7) * 9U;
        const std::uint64_t t = state_[1] << 17U;
        state_[2] ^= state_[0];
        state_[3] ^= state_[1];
        state_[1] ^= state_[2];
        state_[0] ^= state_[3];
        state_[2] ^= t;
        state_[3] = rotl(state_[3], 45);
        return result;
    }

    /// Uniform integer in the inclusive range [lo, hi]. Requires hi >= lo.
    std::uint64_t uniform(std::uint64_t lo, std::uint64_t hi) noexcept {
        const std::uint64_t range = hi - lo + 1U;
        if (range == 0U) return next_u64();  // full 64-bit range
        const std::uint64_t threshold = (0U - range) % range;  // 2^64 mod range
        std::uint64_t r = next_u64();
        while (r < threshold) r = next_u64();  // rejection removes modulo bias
        return lo + (r % range);
    }

    /// Uniform double in [0, 1).
    double uniform01() noexcept {
        return static_cast<double>(next_u64() >> 11U) * (1.0 / 9007199254740992.0);
    }

    bool chance(double probability) noexcept { return uniform01() < probability; }

private:
    static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
        return (x << static_cast<unsigned>(k)) | (x >> static_cast<unsigned>(64 - k));
    }
    std::uint64_t state_[4]{};
};

/// 64-bit FNV-1a, used for trace checksums (reproducibility metadata).
class Fnv1a {
public:
    void add(std::uint64_t v) noexcept {
        for (unsigned i = 0; i < 8U; ++i) {
            hash_ ^= (v >> (8U * i)) & 0xFFU;
            hash_ *= 0x100000001B3ULL;
        }
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
    std::uint64_t hash_ = 0xCBF29CE484222325ULL;
};

}  // namespace memoryforge
