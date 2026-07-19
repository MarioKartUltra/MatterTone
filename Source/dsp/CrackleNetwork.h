// =============================================================================
//  MatterTone POC — CrackleNetwork.h
//  A small heterogeneous collection of overdamped bistable elements s_1..s_N:
//
//      V_j(s_j) = (a_j/4) s_j^4 - (b_j/2) s_j^2 - ell_j(q1, xi) s_j
//      V_net    = (K_c/2)  sum_{(i,j) in E} (s_i - s_j)^2       (ring graph)
//
//      eta_j s_j' = -(a_j s_j^3 - b_j s_j - ell_j)
//                 + K_c (s_{j-1} + s_{j+1} - 2 s_j)
//
//  * Heterogeneity is DETERMINISTIC: per-element (a_j, b_j, eta_j, kappa_j, bias)
//    are drawn from a seeded PRNG (seed = f(noteNumber, userSeed)). Same note +
//    same seed => identical network (reproducible), not per-sample randomness.
//  * ell_j = kappa_j * q1 + bias_j + loadBias : the resonator and the global
//    loading tilt each element. As loading grows, elements cross their saddle
//    node and flip => *pops*. With small K_c flips stay isolated; with large K_c
//    a flip drags its neighbours over their barrier => *cascades / avalanches*.
//  * The q1-dependence of ell_j is a genuine coupling term -kappa_j q1 s_j in H,
//    so the network pushes back on q1 (backReactionForce) — the micro-failures
//    radiate into the audible mode. Energy-symmetric by construction.
//
//  Header-only, pure C++17, no JUCE.
// =============================================================================
#pragma once

#include <cmath>
#include "Config.h"
#include "Rng.h"

namespace matter
{
class CrackleNetwork
{
public:
    static constexpr int N = cfg::kNumCrackle;

    // Default-construct to a SAFE inert network: a_j=1, eta_j=floor (never zero),
    // no coupling. This guarantees step()/energy() are well-defined even before
    // configure() is called (e.g. in the resonator-core free-decay test), so an
    // unconfigured network can never divide by a zero eta.
    CrackleNetwork()
    {
        for (int j = 0; j < N; ++j)
        {
            a[j] = cfg::kBistableQuartic; b[j] = 0.0; eta[j] = cfg::kEtaFloor;
            kappa[j] = 0.0; bias[j] = 0.0; s[j] = 0.0; sdot[j] = 0.0;
        }
        Kc = 0.0;
    }

    // Build the heterogeneous element bank from a deterministic seed.
    //   roughness  : 0 => homogeneous, 1 => wide parameter spread
    //   meanBarrier: nominal well depth b (from Snap Depth / Brittleness)
    //   meanKappa  : nominal coupling of each element to q1 (from Adhesion)
    //   etaMean    : nominal relaxation time-scale (fast => sharp clicks)
    void configure(std::uint64_t seed, double roughness, double meanBarrier,
                   double meanKappa, double etaMean, double networkCoupling)
    {
        Rng rng(seed);
        Kc = networkCoupling > 0.0 ? networkCoupling : 0.0;
        const double r = (roughness < 0.0 ? 0.0 : (roughness > 1.0 ? 1.0 : roughness));

        for (int j = 0; j < N; ++j)
        {
            // log-normal-ish spread around the means, bounded.
            const double gB = std::exp(r * (rng.nextUnit() - 0.5) * 2.0); // ~[e^-r, e^r]
            const double gE = std::exp(r * (rng.nextUnit() - 0.5) * 2.0);
            const double gK = std::exp(r * (rng.nextUnit() - 0.5) * 1.5);

            a[j]     = cfg::kBistableQuartic;
            b[j]     = meanBarrier * gB;
            eta[j]   = (etaMean * gE) > cfg::kEtaFloor ? (etaMean * gE) : cfg::kEtaFloor;
            kappa[j] = meanKappa * gK * (rng.nextUnit() < 0.5 ? 1.0 : -1.0); // random sign
            bias[j]  = meanBarrier * 0.15 * r * rng.nextGaussian();          // static tilt spread
        }
        reset();
    }

    // Reset all elements to (a mildly randomised) rest state near a well minimum.
    void reset()
    {
        for (int j = 0; j < N; ++j)
        {
            // seat each element in a well: sqrt(b/a) if bistable, else 0.
            s[j]    = (b[j] > 0.0) ? -std::sqrt(b[j] / a[j]) : 0.0;
            sdot[j] = 0.0;
        }
    }

    // Stored energy contributed to H (elements + ring coupling), given q1 and the
    // current global loading tilt. loadBias MUST match the tilt used in step() so
    // this is the exact potential the gradient flow descends (energy-monitor
    // consistency).
    double energy(double q1, double loadBias = 0.0) const
    {
        double e = 0.0;
        for (int j = 0; j < N; ++j)
        {
            const double sj  = s[j];
            const double sj2 = sj * sj;
            const double ell = kappa[j] * q1 + bias[j] + loadBias;
            e += 0.25 * a[j] * sj2 * sj2 - 0.5 * b[j] * sj2 - ell * sj;
        }
        if (Kc > 0.0)
            for (int j = 0; j < N; ++j)
            {
                const int jn = (j + 1) % N;
                const double d = s[j] - s[jn];
                e += 0.5 * Kc * d * d;
            }
        return e;
    }

    // Force this network exerts back on q1:  +sum_j kappa_j s_j  = -dH/dq1 part.
    double backReactionForce() const
    {
        double f = 0.0;
        for (int j = 0; j < N; ++j) f += kappa[j] * s[j];
        return f;
    }

    // Advance the whole network one internal sub-step.
    //   q1       : current resonator displacement (loads the elements)
    //   loadBias : global uniform loading tilt (Load/Snap excitation, memory)
    //   dt       : sub-step
    //   maxMove  : per-step displacement cap for stability
    // Outputs (accumulated by the voice for radiation / diagnostics):
    //   eventRate     : sum_j s_j'   (internal-event velocity -> click radiation)
    //   dissipated    : sum_j eta_j (s_j')^2 dt  (>= 0)
    void step(double q1, double loadBias, double dt, double maxMove,
              double& eventRate, double& dissipated)
    {
        eventRate  = 0.0;
        dissipated = 0.0;

        // Compute all right-hand sides from the CURRENT state (explicit neighbour
        // coupling within a sub-step; the whole thing is a stable gradient flow).
        for (int j = 0; j < N; ++j)
        {
            const int jm = (j + N - 1) % N;
            const int jp = (j + 1) % N;

            const double ell   = kappa[j] * q1 + bias[j] + loadBias;
            const double grad  = a[j] * s[j] * s[j] * s[j] - b[j] * s[j] - ell; // dV/ds
            double       coup  = 0.0;
            if (Kc > 0.0) coup = Kc * (s[jm] + s[jp] - 2.0 * s[j]);

            const double rhs = (-grad + coup) / eta[j]; // s_j'
            sdot[j] = rhs;
        }

        // Apply the updates with a per-step motion cap.
        for (int j = 0; j < N; ++j)
        {
            double ds = sdot[j] * dt;
            if (ds >  maxMove) ds =  maxMove;
            if (ds < -maxMove) ds = -maxMove;
            s[j] += ds;

            eventRate  += sdot[j];
            dissipated += eta[j] * sdot[j] * sdot[j] * dt;
        }
    }

    // Numerical-safety check: are all element states finite and bounded?
    bool statesValid() const
    {
        for (int j = 0; j < N; ++j)
            if (!std::isfinite(s[j]) || std::fabs(s[j]) > cfg::kMaxCrackleState)
                return false;
        return true;
    }

    double state(int j) const { return s[j]; }
    double coupling()  const  { return Kc; }

    // Live-editable network coupling (does not re-seed the heterogeneous bank,
    // so turning the knob on a held note does not reset the element states).
    void setCoupling(double networkCoupling) { Kc = networkCoupling > 0.0 ? networkCoupling : 0.0; }

    // Mean absolute element displacement — a compact "material state" indicator.
    double meanAbsState() const
    {
        double s0 = 0.0;
        for (int j = 0; j < N; ++j) s0 += std::fabs(s[j]);
        return s0 / (double) N;
    }

private:
    double a[N]     {};
    double b[N]     {};
    double eta[N]   {};
    double kappa[N] {};
    double bias[N]  {};
    double s[N]     {};
    double sdot[N]  {};
    double Kc = 0.0;
};
} // namespace matter
