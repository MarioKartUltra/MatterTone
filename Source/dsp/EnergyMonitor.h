// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  MatterTone POC — EnergyMonitor.h
//  Debug/diagnostic accounting of the thermodynamic bookkeeping for one voice:
//
//      stored energy H, dissipated power D, injected power P_in,
//      radiated/output proxy, and the numerical energy residual.
//
//  The key admissibility check for the POC is that, with NO external forcing,
//  the discrete stored energy does not systematically grow:
//
//      H_{n+1} <= H_n + eps_num
//
//  within a stated tolerance (tiny bounded integrator error only).
//  The heavy per-step accounting is intended for the offline harness / debug
//  builds; the real-time path calls only the cheap parts.  Pure C++17, no JUCE.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace matter
{
class EnergyMonitor
{
public:
    void reset(double initialEnergy = 0.0)
    {
        Hprev            = initialEnergy;
        Hcur             = initialEnergy;
        dissipatedAccum  = 0.0;
        injectedAccum    = 0.0;
        radiatedAccum    = 0.0;
        maxEnergyRise    = 0.0;
        worstResidual    = 0.0;
        started          = false;
    }

    // Call once per internal sub-step (or per output sample) with the freshly
    // computed stored energy H, and the sub-step's dissipated / injected energy.
    // 'forced' tells the monitor whether external power was injected this step,
    // so the unforced-monotonicity statistic ignores forced steps.
    inline void update(double H, double dissipatedStep, double injectedStep,
                       double radiatedStep, bool forced)
    {
        Hprev = Hcur;
        Hcur  = H;

        dissipatedAccum += dissipatedStep;
        injectedAccum   += injectedStep;
        radiatedAccum   += radiatedStep;

        if (started && !forced)
        {
            const double rise = Hcur - Hprev;               // >0 means energy grew
            if (rise > maxEnergyRise) maxEnergyRise = rise;

            // Discrete energy balance residual: dH should equal injected - dissipated.
            const double residual = std::fabs((Hcur - Hprev) - (injectedStep - dissipatedStep));
            if (residual > worstResidual) worstResidual = residual;
        }
        started = true;
    }

    double storedEnergy()      const { return Hcur; }
    double dissipatedEnergy()  const { return dissipatedAccum; }
    double injectedEnergy()    const { return injectedAccum; }
    double radiatedEnergy()    const { return radiatedAccum; }

    // Largest single-step UNFORCED energy increase seen (should be ~ eps_num).
    double maxUnforcedRise()   const { return maxEnergyRise; }
    // Worst discrete energy-balance residual seen.
    double worstBalanceResidual() const { return worstResidual; }

private:
    double Hprev           = 0.0;
    double Hcur            = 0.0;
    double dissipatedAccum = 0.0;
    double injectedAccum   = 0.0;
    double radiatedAccum   = 0.0;
    double maxEnergyRise   = 0.0;
    double worstResidual   = 0.0;
    bool   started         = false;
};
} // namespace matter
