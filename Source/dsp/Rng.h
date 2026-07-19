// =============================================================================
//  MatterTone POC — Rng.h
//  Deterministic, seedable PRNG (SplitMix64 -> xoshiro-free, allocation-free).
//  Used for TWO things only:
//    1. seeded, repeatable HETEROGENEITY of the crackle network (per note);
//    2. bounded "agitation" stochastic forcing (optional, off by default).
//  It is never allowed to be the primary sound source (see brief coding rules).
//  Pure C++17, no <random> (that is neither guaranteed reproducible nor RT-safe).
// =============================================================================
#pragma once

#include <cstdint>

namespace matter
{
class Rng
{
public:
    explicit Rng(std::uint64_t seed = 1) { reseed(seed); }

    inline void reseed(std::uint64_t seed)
    {
        // SplitMix64 state init; avoid the degenerate all-zero state.
        state = seed ? seed : 0x9E3779B97F4A7C15ull;
    }

    // Uniform in [0,1).
    inline double nextUnit()
    {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z =  z ^ (z >> 31);
        // top 53 bits -> double in [0,1)
        return (z >> 11) * (1.0 / 9007199254740992.0);
    }

    // Uniform in [lo, hi].
    inline double nextRange(double lo, double hi) { return lo + (hi - lo) * nextUnit(); }

    // Approx standard normal via sum-of-uniforms (Irwin–Hall, k=12): mean 0, var 1.
    // Cheap, bounded in [-6,6], adequate for bounded agitation forcing.
    inline double nextGaussian()
    {
        double s = 0.0;
        for (int i = 0; i < 12; ++i) s += nextUnit();
        return s - 6.0;
    }

private:
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
};
} // namespace matter
