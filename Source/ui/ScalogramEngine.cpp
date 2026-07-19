// =============================================================================
//  MatterTone POC — ScalogramEngine.cpp   (compiled only when MATTER_USE_FCWT)
// =============================================================================
#include "ScalogramEngine.h"

#include <cmath>
#include <complex>
#include <algorithm>

#include "fcwt.h"   // pulls in fCWT + fftw3 + `using namespace std` — isolated here

namespace matter
{
struct ScalogramEngine::Impl
{
    std::unique_ptr<Morlet> morlet;   // declared first: scales/fcwt reference it
    std::unique_ptr<Scales> scales;
    std::unique_ptr<FCWT>   fcwt;
    std::vector<std::complex<float>> out;
};

ScalogramEngine::ScalogramEngine()  = default;
ScalogramEngine::~ScalogramEngine() = default;

void ScalogramEngine::prepare(double sampleRate, const ScalogramConfig& cfg)
{
    sr      = sampleRate > 1.0 ? sampleRate : 48000.0;
    cfg_    = cfg;
    win     = std::max(64, cfg.windowSize);
    nScales = std::max(4, cfg.numScales);

    const float nyq = (float) (sr * 0.5);
    float f0 = std::max(1.0f, cfg.f0);
    float f1 = std::min(cfg.f1, 0.45f * (float) sr);
    if (f1 <= f0) f1 = std::min(nyq * 0.9f, f0 * 8.0f);

    impl = std::make_unique<Impl>();
    impl->morlet = std::make_unique<Morlet>(std::max(0.3f, cfg.bandwidth));
    impl->scales = std::make_unique<Scales>(impl->morlet.get(),
                                            cfg.logScales ? FCWT_LOGSCALES : FCWT_LINSCALES,
                                            (int) sr, f0, f1, nScales);
    impl->fcwt   = std::make_unique<FCWT>(impl->morlet.get(), 1, false, false);
    impl->out.assign((size_t) nScales * win, std::complex<float>(0.0f, 0.0f));

    freqs.assign((size_t) nScales, 0.0f);
    impl->scales->getFrequencies(freqs.data(), nScales);

    mags.assign((size_t) nScales * win, 0.0f);
    inbuf.assign((size_t) win, 0.0f);
}

void ScalogramEngine::process(const float* samples, int n)
{
    if (!ready() || n < win) return;

    const float* src = samples + (n - win);
    std::copy(src, src + win, inbuf.begin());
    impl->fcwt->cwt(inbuf.data(), win, impl->out.data(), impl->scales.get());

    // Per-scale L1/L2 weighting, then global-max normalisation to [0,1].
    // L2 (energy) = raw magnitude. L1 (amplitude) divides out the ~sqrt(scale)
    // growth by boosting with sqrt(freq/f_ref) so a constant-amplitude tone reads
    // flat across scales.
    float fref = 1.0f;
    for (int s = 0; s < nScales; ++s) fref = std::max(fref, freqs[(size_t) s]);

    float maxMag = 1.0e-12f;
    for (int s = 0; s < nScales; ++s)
    {
        const float w = cfg_.l1norm ? std::sqrt(std::max(1.0f, freqs[(size_t) s]) / fref) : 1.0f;
        for (int t = 0; t < win; ++t)
        {
            const float m = std::abs(impl->out[(size_t) s * win + t]) * w;
            mags[(size_t) s * win + t] = m;
            if (m > maxMag) maxMag = m;
        }
    }
    const float inv = 1.0f / maxMag;
    for (float& m : mags) m *= inv;
}
} // namespace matter
