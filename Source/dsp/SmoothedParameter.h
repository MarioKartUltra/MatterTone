// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  MatterTone POC — SmoothedParameter.h
//  One-pole exponential smoother for any control that enters the differential
//  equations. Smoothing prevents zipper noise and, more importantly, keeps the
//  ODE right-hand side continuous when a user drags a slider.  Pure C++17.
// =============================================================================
#pragma once

#include <cmath>

namespace matter
{
class SmoothedParameter
{
public:
    void reset(double sampleRate, double smoothingSeconds = 0.02)
    {
        // coeff for a one-pole smoother reaching ~63% in smoothingSeconds
        const double t = (smoothingSeconds > 1.0e-6) ? smoothingSeconds : 1.0e-6;
        coeff = std::exp(-1.0 / (t * (sampleRate > 1.0 ? sampleRate : 1.0)));
        // 'current' left untouched so we can snap explicitly.
    }

    void snap(double value)   { current = target = value; }
    void setTarget(double v)  { target = v; }
    double getTarget() const  { return target; }

    // Advance one (output-rate) sample and return the smoothed value.
    inline double next()
    {
        current = target + (current - target) * coeff;
        return current;
    }

    inline double value() const { return current; }

private:
    double coeff   = 0.0;
    double current = 0.0;
    double target  = 0.0;
};
} // namespace matter
