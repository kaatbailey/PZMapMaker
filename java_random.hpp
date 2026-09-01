// java_random.hpp — bit-exact port of java.util.Random.
//
// WHY THIS EXISTS. Six of Track F's fourteen SHIPS units draw randomness:
// GisCells (per-cell, per-dwelling and per-building seeds), GroundRegions,
// MaskRule, TreeScatter, GroundPalette and BuildingPlan. std::mt19937 is a
// different generator entirely and reproduces none of them, so without this
// every downstream oracle diff is noise and F6's byte-identical mod output can
// never clear. It is ported before anything that draws, not alongside it.
//
// The algorithm is specified exactly in the java.util.Random javadoc — it is a
// 48-bit linear congruential generator, and the spec is normative, so this is a
// port of a documented contract rather than a guess at an implementation.
//
// nextInt(bound) is the subtle one: it is NOT next(31) % bound. Java rejects
// and redraws to remove modulo bias, and has a special case for powers of two.
// Getting that wrong produces a generator that looks fine in isolation and
// diverges from Java only on some seeds — the worst possible failure mode for
// an oracle.

#pragma once

#include <cstdint>

namespace pzformat {

class JavaRandom {
public:
    explicit JavaRandom(std::int64_t seed) { setSeed(seed); }

    void setSeed(std::int64_t seed) {
        seed_ = (static_cast<std::uint64_t>(seed) ^ kMultiplier) & kMask;
        haveNextNextGaussian_ = false;
    }

    /// Java: protected int next(int bits)
    std::int32_t next(int bits) {
        seed_ = (seed_ * kMultiplier + kAddend) & kMask;
        return static_cast<std::int32_t>(seed_ >> (48 - bits));
    }

    std::int32_t nextInt() { return next(32); }

    /// Java: public int nextInt(int bound)
    std::int32_t nextInt(std::int32_t bound) {
        // Java throws IllegalArgumentException for bound <= 0; callers in this
        // tree never do, and reproducing the throw would not help the oracle.
        std::int32_t r = next(31);
        const std::int32_t m = bound - 1;
        if ((bound & m) == 0) {
            // Power of two: take the high bits.
            return static_cast<std::int32_t>(
                (static_cast<std::int64_t>(bound) * static_cast<std::int64_t>(r)) >> 31);
        }
        for (std::int32_t u = r; u - (r = u % bound) + m < 0; u = next(31)) {
            // Reject and redraw on overflow, exactly as Java does.
        }
        return r;
    }

    std::int64_t nextLong() {
        const std::int64_t hi = static_cast<std::int64_t>(next(32)) << 32;
        return hi + static_cast<std::int64_t>(next(32));
    }

    bool nextBoolean() { return next(1) != 0; }

    float nextFloat() { return static_cast<float>(next(24)) / static_cast<float>(1 << 24); }

    /// Java: public double nextDouble()
    double nextDouble() {
        const std::int64_t hi = static_cast<std::int64_t>(next(26)) << 27;
        const std::int64_t v = hi + static_cast<std::int64_t>(next(27));
        return static_cast<double>(v) * kDoubleUnit;
    }

private:
    static constexpr std::uint64_t kMultiplier = 0x5DEECE66DULL;
    static constexpr std::uint64_t kAddend = 0xBULL;
    static constexpr std::uint64_t kMask = (1ULL << 48) - 1;
    static constexpr double kDoubleUnit = 1.0 / static_cast<double>(1LL << 53);

    std::uint64_t seed_ = 0;
    bool haveNextNextGaussian_ = false;
};

}  // namespace pzformat
