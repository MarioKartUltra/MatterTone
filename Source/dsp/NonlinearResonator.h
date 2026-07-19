// =============================================================================
//  MatterTone POC — NonlinearResonator.h
//  ONE nonlinear resonant mode with unit mass:
//
//      q'' + c q' + c3 |q'| q' + k q + beta q^3 + delta q^5 = F_ext
//
//  Stored energy of the mode (the part of H owned by this mode):
//
//      E = p^2/2 + k q^2/2 + beta q^4/4 + delta q^6/6      (p = q', m = 1)
//
//  Admissibility guarantees enforced here:
//    * delta > 0 ALWAYS (positive highest-order term) => E bounded below even
//      when beta < 0 (softening).  See Config::kSexticFloor.
//    * damping is applied in a dedicated dissipative sub-step that can only
//      REMOVE kinetic energy (exact exponential for the linear part, a
//      backward-Euler-style step for the |v|v part). Both give D >= 0.
//
//  This struct only holds coefficients + pure helpers; the (q,p) state lives in
//  the voice's state vector so the integrator can treat all modes uniformly.
//  Header-only, pure C++17, no JUCE.
// =============================================================================
#pragma once

#include <cmath>
#include "Config.h"

namespace matter
{
struct NonlinearResonator
{
    double k     = 1.0;   // linear stiffness  (= omega^2 for unit mass)
    double beta  = 0.0;   // cubic coefficient (sign = hardening/softening)
    double delta = cfg::kSexticFloor; // quintic-force / sextic-energy coeff, > 0
    double c     = 0.0;   // linear damping   >= 0
    double c3    = 0.0;   // nonlinear |v|v damping >= 0
    double omega = 1.0;   // sqrt(k), cached reference angular frequency

    void configure(double stiffness, double cubic, double sextic,
                   double linDamp, double nonlinDamp)
    {
        k     = stiffness > 0.0 ? stiffness : 1.0;
        beta  = cubic;
        delta = sextic > cfg::kSexticFloor ? sextic : cfg::kSexticFloor;
        c     = linDamp    > 0.0 ? linDamp    : 0.0;
        c3    = nonlinDamp > 0.0 ? nonlinDamp : 0.0;
        omega = std::sqrt(k);
    }

    // Conservative restoring force of this mode alone (no coupling, no external).
    inline double selfForce(double q) const
    {
        const double q2 = q * q;
        return -(k * q + beta * q2 * q + delta * q2 * q2 * q);
    }

    // Stored energy of this mode.
    inline double energy(double q, double p) const
    {
        const double q2 = q * q;
        return 0.5 * p * p
             + 0.5 * k * q2
             + 0.25 * beta * q2 * q2
             + (delta / 6.0) * q2 * q2 * q2;
    }

    // Dissipative momentum update over dt. Returns dissipated energy (>= 0).
    // Applied AFTER the conservative (symplectic) kick-drift-kick.
    inline double applyDamping(double& p, double dt) const
    {
        const double pBefore = p;

        // Nonlinear |v|v drag, backward-Euler style: unconditionally |p| shrinks.
        if (c3 > 0.0)
            p = p / (1.0 + c3 * std::fabs(p) * dt);

        // Linear drag, exact exponential: p *= exp(-c dt).
        if (c > 0.0)
            p *= std::exp(-c * dt);

        // Kinetic energy removed (m = 1): 1/2 (pBefore^2 - p^2) >= 0.
        return 0.5 * (pBefore * pBefore - p * p);
    }
};
} // namespace matter
