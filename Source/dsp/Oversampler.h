// =============================================================================
//  MatterTone POC — Oversampler.h
//  The nonlinear ODE is integrated at (sampleRate * factor); the radiation signal
//  therefore exists on that oversampled grid. This file provides the DECIMATION
//  side: a 4th-order Butterworth low-pass (two cascaded biquads) run at the
//  oversampled rate, from which we take one anti-aliased sample per host sample.
//
//  Nonlinear forces (contact, snap, crackle, hardening) generate energy well
//  above the base Nyquist; oversampling + this decimation filter keep that from
//  folding back as harsh aliasing.  Header-only, pure C++17, no JUCE.
// =============================================================================
#pragma once

#include <cmath>

namespace matter
{
// Transposed-direct-form-II biquad.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0.0; }

    // RBJ low-pass at cutoff f0 (Hz) for sample rate fs (Hz), quality q.
    void setLowpass(double f0, double fs, double q)
    {
        if (f0 > 0.45 * fs) f0 = 0.45 * fs;        // keep below Nyquist
        const double w0    = 2.0 * M_PI * f0 / fs;
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * q);
        const double a0    = 1.0 + alpha;

        b0 = (1.0 - cosw0) * 0.5 / a0;
        b1 = (1.0 - cosw0)       / a0;
        b2 = (1.0 - cosw0) * 0.5 / a0;
        a1 = (-2.0 * cosw0)      / a0;
        a2 = (1.0 - alpha)       / a0;
    }

    inline double process(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

class Decimator
{
public:
    // outputSampleRate = host rate; factor = oversampling (2/4/8).
    void prepare(double outputSampleRate, int factor)
    {
        os      = factor > 1 ? factor : 1;
        const double fsOS = outputSampleRate * os;
        const double fc   = 0.45 * outputSampleRate;  // just under the base Nyquist
        // 4th-order Butterworth as two biquads (Q values for maximally flat).
        s1.setLowpass(fc, fsOS, 0.54119610);
        s2.setLowpass(fc, fsOS, 1.30656296);
        reset();
    }

    void reset() { s1.reset(); s2.reset(); }

    // Feed one oversampled sample through the anti-alias filter.
    inline double processOversampled(double x) { return s2.process(s1.process(x)); }

    int factor() const { return os; }

private:
    Biquad s1, s2;
    int    os = 1;
};
} // namespace matter
