// =============================================================================
//  MatterTone POC — BistableElement.h
//  A single overdamped bistable ("snap") element in a tilted double well:
//
//      V(s; lambda) = (a/4) s^4 - (b/2) s^2 - lambda s          (a > 0)
//      eta s' = -dV/ds + drive  =  -(a s^3 - b s - lambda) + drive
//
//  With a > 0 the potential is bounded below for every finite (b, lambda), so it
//  contributes a bounded-below term to H.  The gradient flow eta s' = -dV/ds is
//  intrinsically dissipative in V: dV/dt = V'(s) s' = -(1/eta) V'(s)^2 <= 0.
//
//  A *snap* is an EMERGENT saddle-node event: as lambda (loading) ramps, one of
//  the two minima disappears and the state falls to the surviving well. Nothing
//  is scripted or sampled — the discontinuity in the observable comes from the
//  bifurcation of the potential.  Header-only, pure C++17, no JUCE.
// =============================================================================
#pragma once

#include <cmath>
#include "Config.h"

namespace matter
{
struct BistableElement
{
    double a      = cfg::kBistableQuartic; // quartic coeff, > 0
    double b      = 1.0;                    // negative-quadratic coeff (well depth/barrier)
    double lambda = 0.0;                    // linear tilt (loading)
    double eta    = 0.01;                   // overdamped relaxation time-scale (> 0)

    void configure(double quartic, double wellDepth, double relaxTime)
    {
        a   = quartic  > 0.0 ? quartic  : cfg::kBistableQuartic;
        b   = wellDepth;                       // may be <=0 (monostable) — still bounded
        eta = relaxTime > cfg::kEtaFloor ? relaxTime : cfg::kEtaFloor;
    }

    // Potential value (contributes to stored energy H).
    inline double energy(double s) const
    {
        const double s2 = s * s;
        return 0.25 * a * s2 * s2 - 0.5 * b * s2 - lambda * s;
    }

    // -dV/ds  = the deterministic drift toward lower potential.
    inline double negGradient(double s) const
    {
        return -(a * s * s * s - b * s - lambda);
    }

    // Overdamped update with an external drive term (e.g. g_s * q1), integrated
    // as a stable step whose per-step motion is capped so a stiff well cannot
    // overshoot.  Returns the dissipated power contribution (>= 0) for diagnostics.
    //   eta s' = negGradient(s) + drive
    inline double step(double& s, double drive, double dt, double maxMove)
    {
        const double rhs   = (negGradient(s) + drive) / eta;      // s'
        double       ds    = rhs * dt;
        if (ds >  maxMove) ds =  maxMove;
        if (ds < -maxMove) ds = -maxMove;
        s += ds;
        // Gradient-flow dissipation rate ~ eta * (s')^2 >= 0 (drive-injected part
        // is accounted separately at the voice level via power exchange).
        return eta * rhs * rhs * dt;
    }

    // Convenience: is the potential currently a double well? (b > 0 with small tilt)
    inline bool isBistable() const { return b > 0.0; }
};
} // namespace matter
