// =============================================================================
//  MatterTone POC — PitchDetector.h
//  Cheap autocorrelation dominant-frequency estimate used to REPORT the measured
//  pitch that emerges after the attack (the output pitch is not forced to f_ref).
//  Intended for the offline harness and the UI/background thread — never the
//  audio thread.  Header-only, pure C++17, no JUCE.
// =============================================================================
#pragma once

#include <cmath>
#include <vector>

namespace matter
{
class PitchDetector
{
public:
    // Estimate the dominant frequency (Hz) of 'data' (n samples at sampleRate).
    // Returns 0 if no confident pitch is found (e.g. noise-like crackle).
    static double estimate(const float* data, int n, double sampleRate,
                           double fMin = 40.0, double fMax = 2000.0)
    {
        if (n < 64 || sampleRate <= 0.0) return 0.0;

        // Remove DC.
        double mean = 0.0;
        for (int i = 0; i < n; ++i) mean += data[i];
        mean /= n;

        const int minLag = (int) std::floor(sampleRate / fMax);
        const int maxLag = (int) std::ceil (sampleRate / fMin);
        const int hiLag  = (maxLag < n - 1) ? maxLag : (n - 1);
        if (minLag < 1 || hiLag <= minLag) return 0.0;

        // Zero-lag energy for normalisation.
        double energy0 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double x = data[i] - mean;
            energy0 += x * x;
        }
        if (energy0 < 1.0e-20) return 0.0;

        double bestVal = -1.0;
        int    bestLag = -1;
        double prev = -1.0, prevPrev = -1.0;

        for (int lag = minLag; lag <= hiLag; ++lag)
        {
            double acc = 0.0;
            for (int i = 0; i + lag < n; ++i)
                acc += (data[i] - mean) * (data[i + lag] - mean);
            const double norm = acc / energy0;

            if (norm > bestVal)
            {
                bestVal = norm;
                bestLag = lag;
            }
            prevPrev = prev;
            prev     = norm;
            (void) prevPrev;
        }

        // Require a reasonably periodic signal.
        if (bestLag < 1 || bestVal < 0.2) return 0.0;

        // Parabolic interpolation around the peak lag for sub-sample accuracy.
        double lagRefined = bestLag;
        if (bestLag > minLag && bestLag < hiLag)
        {
            const double ym1 = autocorrAt(data, n, mean, bestLag - 1);
            const double y0  = autocorrAt(data, n, mean, bestLag);
            const double yp1 = autocorrAt(data, n, mean, bestLag + 1);
            const double denom = (ym1 - 2.0 * y0 + yp1);
            if (std::fabs(denom) > 1.0e-20)
                lagRefined = bestLag + 0.5 * (ym1 - yp1) / denom;
        }

        return (lagRefined > 0.0) ? sampleRate / lagRefined : 0.0;
    }

    static double estimate(const std::vector<float>& v, double sampleRate,
                           double fMin = 40.0, double fMax = 2000.0)
    {
        return estimate(v.data(), (int) v.size(), sampleRate, fMin, fMax);
    }

private:
    static double autocorrAt(const float* data, int n, double mean, int lag)
    {
        double acc = 0.0;
        for (int i = 0; i + lag < n; ++i)
            acc += (data[i] - mean) * (data[i + lag] - mean);
        return acc;
    }
};
} // namespace matter
