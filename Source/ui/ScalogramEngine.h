// =============================================================================
//  MatterTone POC — ScalogramEngine.h
//  Thin, JUCE-free, header-clean wrapper around fCWT. pImpl hides fcwt.h from
//  every JUCE translation unit. UI thread only. Enabled with MATTER_USE_FCWT.
//
//  Configurable: Morlet bandwidth (the mother-wavelet shape knob — fCWT ships
//  Morlet only), scale range [f0,f1], scale count, log-vs-linear scale spacing,
//  and L1 (amplitude) vs L2 (energy) coefficient normalization.
// =============================================================================
#pragma once

#include <vector>
#include <memory>

namespace matter
{
struct ScalogramConfig
{
    int   windowSize = 2048;
    int   numScales  = 64;
    float f0         = 40.0f;      // lowest analysed frequency (Hz)
    float f1         = 16000.0f;   // highest analysed frequency (Hz, clamped < Nyquist)
    float bandwidth  = 2.0f;       // Morlet sigma (mother-wavelet shape)
    bool  logScales  = true;       // FCWT_LOGSCALES vs FCWT_LINSCALES
    bool  l1norm     = false;      // L1 amplitude-preserving vs L2 energy-preserving

    bool operator==(const ScalogramConfig& o) const
    {
        return windowSize == o.windowSize && numScales == o.numScales
            && f0 == o.f0 && f1 == o.f1 && bandwidth == o.bandwidth
            && logScales == o.logScales && l1norm == o.l1norm;
    }
    bool operator!=(const ScalogramConfig& o) const { return !(*this == o); }
};

class ScalogramEngine
{
public:
    ScalogramEngine();
    ~ScalogramEngine();

    void prepare(double sampleRate, const ScalogramConfig& cfg);

    // Compute |CWT| over the LAST windowSize samples of `samples` (n >= windowSize),
    // apply the L1/L2 per-scale weighting, and normalise the matrix to [0,1].
    //   magnitudes()[scale * windowSize + time]
    void process(const float* samples, int n);

    int   numScales()  const { return nScales; }
    int   windowSize() const { return win; }
    float freqOf(int scale) const { return (scale >= 0 && scale < (int) freqs.size()) ? freqs[(size_t) scale] : 0.0f; }
    const std::vector<float>& magnitudes() const { return mags; }
    const ScalogramConfig& config() const { return cfg_; }
    double sampleRate() const { return sr; }
    bool  ready() const { return impl != nullptr && win > 0; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    ScalogramConfig cfg_;
    int    nScales = 0;
    int    win     = 0;
    double sr      = 48000.0;
    std::vector<float> freqs;
    std::vector<float> mags;
    std::vector<float> inbuf;
};
} // namespace matter
