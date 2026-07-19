// =============================================================================
//  MatterTone POC — ContactModel.h
//  Smooth unilateral (one-sided) contact against a wall at position xWall.
//  Penetration  x = q - xWall  (contact only when x > 0).  Hertz-like law:
//
//      F_c = -k_c <x>_+^{3/2}  -  c_c <x>_+^{1/2} x'
//
//  The elastic part derives from a bounded-below potential
//
//      V_contact(x) = (2/5) k_c <x>_+^{5/2}   (>= 0),   F_elastic = -dV/dx
//
//  so it contributes admissibly to H.  The damping part removes energy while in
//  contact: its power is  -c_c <x>_+^{1/2} (x')^2 <= 0.
//
//  The bare positive-part <x>_+ has a kink at x=0 (derivative discontinuity) that
//  aliases badly and can destabilise the solver. We regularise it with a smooth
//  softplus-like ramp of width eps so <x>_+ and its derivative are continuous.
//  Header-only, pure C++17, no JUCE.
// =============================================================================
#pragma once

#include <cmath>
#include "Config.h"

namespace matter
{
struct ContactModel
{
    double kC    = 0.0;                 // Hertz stiffness   >= 0
    double cC    = 0.0;                 // contact damping   >= 0
    double xWall = 1.0;                 // wall location in displacement units
    double eps   = cfg::kContactSmooth; // regularisation width, > 0

    void configure(double hardness, double loss, double wallPos)
    {
        kC    = hardness > 0.0 ? hardness : 0.0;
        cC    = loss     > 0.0 ? loss     : 0.0;
        xWall = wallPos;
    }

    // Smooth, C1 positive part: ~0 for x << -eps, ~x for x >> eps, rounded at 0.
    // softplus form: (eps) * log(1 + exp(x/eps)), clamped for large |x/eps|.
    static inline double smoothPos(double x, double eps)
    {
        const double z = x / eps;
        if (z >  30.0) return x;                 // avoid overflow: ~ x
        if (z < -30.0) return 0.0;               // underflow: ~ 0
        return eps * std::log1p(std::exp(z));
    }

    // Elastic contact energy V_contact >= 0 (for the energy monitor / H).
    inline double energy(double q) const
    {
        if (kC <= 0.0) return 0.0;
        const double xp = smoothPos(q - xWall, eps);
        // (2/5) k_c xp^{5/2}
        return 0.4 * kC * std::pow(xp, 2.5);
    }

    // Elastic (conservative) contact force only:  -k_c <x>_+^{3/2}.
    // This is the gradient of V_contact and belongs in the symplectic sub-step.
    inline double elasticForce(double q) const
    {
        if (kC <= 0.0) return 0.0;
        const double xp = smoothPos(q - xWall, eps);
        if (xp <= 0.0) return 0.0;
        return -kC * xp * std::sqrt(xp);
    }

    // Velocity-linear damping COEFFICIENT c_c <x>_+^{1/2} (>= 0). The voice folds
    // this into the dissipative sub-step as p /= (1 + coeff*dt), which can only
    // remove energy — contact damping never injects energy.
    inline double dampingCoeff(double q) const
    {
        if (cC <= 0.0) return 0.0;
        const double xp = smoothPos(q - xWall, eps);
        if (xp <= 0.0) return 0.0;
        return cC * std::sqrt(xp);
    }

    // Combined force (elastic + damping) — convenience for radiation/diagnostics.
    //   qdot is the wall-normal velocity (= p for unit mass).
    // Also returns, via dissipatedPower, the (>=0) power removed by contact damping.
    inline double force(double q, double qdot, double& dissipatedPower) const
    {
        dissipatedPower = 0.0;
        if (kC <= 0.0 && cC <= 0.0) return 0.0;

        const double xp = smoothPos(q - xWall, eps);
        if (xp <= 0.0) return 0.0;

        const double root = std::sqrt(xp);          // <x>_+^{1/2}
        const double fEl  = -kC * xp * root;         // -k_c xp^{3/2}
        const double fDmp = -cC * root * qdot;       // -c_c xp^{1/2} qdot
        dissipatedPower   =  cC * root * qdot * qdot; // >= 0
        return fEl + fDmp;
    }
};
} // namespace matter
