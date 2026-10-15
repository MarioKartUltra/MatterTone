// =============================================================================
//  MatterTone POC — matter_render.cpp
//  Offline, JUCE-free command-line harness. Two jobs:
//    1. RENDER each factory preset to a short WAV (development verification;
//       NOT a substitute for real-time synthesis).
//    2. TEST the DSP core against the brief's required properties (energy decay,
//       equilibrium, dissipation sign, bounded stress, sample-rate consistency,
//       determinism, event emergence, silence-after-decay).
//
//  Usage:  matter_render [--out DIR] [--sr HZ] [--os N] [--test-only] [--render-only]
//  Exit code = number of failed tests (0 = all passed).
// =============================================================================
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#include "../Source/dsp/MatterSynth.h"
#include "../Source/dsp/Presets.h"
#include "../Source/dsp/PitchDetector.h"
#include "../Source/dsp/BistableElement.h"
#include "../Source/dsp/NonlinearResonator.h"
#include "../Source/dsp/CrackleNetwork.h"
#include "../Source/common/acv2/dsp/Lfo.h"
#include "../Source/common/acv2/dsp/Units.h"
#include "WavFile.h"

using namespace matter;

// -----------------------------------------------------------------------------
struct RenderStats { double peak = 0, rms = 0, domFreq = 0; int resets = 0; };

static std::vector<float> renderNote(const MatterParams& params, int note, double vel,
                                     double sr, int os, double totalSecs, double releaseSecs,
                                     RenderStats* stats = nullptr)
{
    MatterSynth synth;
    MatterParams p = params;
    p.oversampling = os;
    synth.prepare(sr, 512);
    synth.setParameters(p);

    const int total = (int) std::lround(totalSecs * sr);
    const int relAt  = (int) std::lround(releaseSecs * sr);
    std::vector<float> out((size_t) total, 0.0f);

    const int block = 128;
    std::vector<float> L((size_t) block), R((size_t) block);
    bool noteIsOn = false;
    for (int n = 0; n < total; )
    {
        if (!noteIsOn && n == 0) { synth.noteOn(note, vel); noteIsOn = true; }
        if (noteIsOn && n >= relAt) { synth.noteOff(note); noteIsOn = false; }

        const int m = std::min(block, total - n);
        synth.renderBlock(L.data(), R.data(), m);
        for (int i = 0; i < m; ++i) out[(size_t) (n + i)] = L[(size_t) i];
        n += m;
    }

    if (stats)
    {
        double peak = 0, sumsq = 0;
        for (float s : out) { const double a = std::fabs(s); if (a > peak) peak = a; sumsq += (double) s * s; }
        stats->peak = peak;
        stats->rms  = std::sqrt(sumsq / std::max<size_t>(1, out.size()));
        stats->resets = synth.totalResets();
        // dominant frequency from a post-attack window (skip 60 ms attack).
        const int start = std::min((int) (0.06 * sr), total / 4);
        const int len   = std::min((int) (0.35 * sr), total - start);
        if (len > 256)
            stats->domFreq = PitchDetector::estimate(out.data() + start, len, sr);
    }
    return out;
}

// -----------------------------------------------------------------------------
static int failures = 0;
static void check(bool cond, const char* name, const std::string& detail = "")
{
    std::printf("  [%s] %s%s%s\n", cond ? "PASS" : "FAIL", name,
                detail.empty() ? "" : "  -> ", detail.c_str());
    if (!cond) ++failures;
}


// ---- Test: a knob moved while a note sounds must not click ---------------------
// Parameters arrive once per block. Without smoothing, each step lands as a kink
// in the waveform at the block boundary: the second difference there is far
// larger than anywhere inside the blocks. A clean tone (no crackle, no
// agitation) makes the interior second difference tiny, so the ratio is sharp.
static void testParameterSmoothing(double sr, int os)
{
    std::printf("Test: parameter smoothing (no clicks at block boundaries)\n");
    auto sweep = [&](const char* name, auto&& apply, bool percussion = false)
    {
        MatterSynth synth;
        MatterParams p;
        p.oversampling = os; p.crackleCoupling = 0.0; p.agitation = 0.0; p.brittleness = 0.0;
        p.snapDepth = 0.0; p.nonlinStiffness = 0.1; p.damping = 0.0; p.memory = 0.0; p.eventMix = 0.0;
        p.attack = 0.0;
        if (percussion) { p.engine = EngineMode::Percussion; p.wireAmount = 0.0; p.dampTilt = 0.1; }
        apply(p, 0.0);
        synth.prepare(sr, 512);
        synth.setParameters(p);
        synth.noteOn(57, 0.8);                               // 220 Hz pluck

        // the pluck decays within a few hundred ms: sweep while it is audible,
        // after the onset ramp, and judge each block against its own interior
        constexpr int block = 512;
        const int settle = (int) (0.04 * sr) / block, move = (int) (0.40 * sr) / block;
        std::vector<float> L((size_t) block), R((size_t) block);
        float prev1 = 0.0f, prev2 = 0.0f;
        for (int b = 0; b < settle; ++b)
        {
            synth.renderBlock(L.data(), R.data(), block);
            prev2 = L[(size_t) block - 2]; prev1 = L[(size_t) block - 1];
        }
        double worst = 0.0; int judged = 0;
        for (int b = 0; b < move; ++b)
        {
            apply(p, (double) b / (double) (move - 1));
            synth.setParameters(p);
            synth.renderBlock(L.data(), R.data(), block);
            double boundary = 0.0, interior = 0.0, peak = 0.0;
            for (int n = 0; n < block; ++n)
            {
                const double y0 = L[(size_t) n];
                const double y1 = n >= 1 ? L[(size_t) n - 1] : prev1;
                const double y2 = n >= 2 ? L[(size_t) n - 2] : (n == 1 ? prev1 : prev2);
                const double d2 = std::fabs(y0 - 2.0 * y1 + y2);
                peak = std::max(peak, std::fabs(y0));
                if (n < 2) boundary = std::max(boundary, d2);
                else       interior = std::max(interior, d2);
            }
            prev2 = L[(size_t) block - 2]; prev1 = L[(size_t) block - 1];
            if (peak < 1.0e-4) continue;                     // inaudible: not judged
            worst = std::max(worst, boundary / std::max(interior, 1.0e-12));
            ++judged;
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "worst boundary/interior kink = %.1f over %d blocks", worst, judged);
        check(judged >= 8 && worst < 3.0, name, buf);
    };
    sweep("output gain sweep is click-free",  [](MatterParams& p, double f) { p.output  = 0.2 + 0.7 * f; });
    sweep("tension sweep is click-free",      [](MatterParams& p, double f) { p.tension = 0.8 + 0.8 * f; });
    sweep("damping sweep is click-free",      [](MatterParams& p, double f) { p.damping = 0.0 + 0.5 * f; });
    sweep("coupling sweep is click-free",     [](MatterParams& p, double f) { p.coupling = 0.0 + 0.9 * f; });
    sweep("body mix sweep is click-free",     [](MatterParams& p, double f) { p.bodyMix = 0.0 + 1.0 * f; });
    sweep("event mix sweep is click-free",    [](MatterParams& p, double f) { p.eventMix = 0.0 + 1.0 * f; });
    sweep("agc sweep is click-free",          [](MatterParams& p, double f) { p.agc = 0.0 + 1.0 * f; });
    sweep("hardening sweep is click-free",    [](MatterParams& p, double f) { p.hardening = -1.0 + 2.0 * f; });
    sweep("nonlinear stiffness sweep is click-free", [](MatterParams& p, double f) { p.nonlinStiffness = 0.0 + 1.0 * f; });
    sweep("mode ratio sweep is click-free",   [](MatterParams& p, double f) { p.modeRatio = 1.2 + 1.5 * f; });
    sweep("contact hardness sweep is click-free", [](MatterParams& p, double f) { p.contactHardness = 0.0 + 1.0 * f; });
    sweep("strike position sweep is click-free",  [](MatterParams& p, double f) { p.strikePosition = 0.0 + 1.0 * f; });
    sweep("nonlinear damping sweep is click-free", [](MatterParams& p, double f) { p.nonlinDamping = 0.0 + 1.0 * f; });
    // the percussion engine is reconfigured every block from the parameters
    sweep("percussion: output gain sweep is click-free", [](MatterParams& p, double f) { p.output = 0.2 + 0.7 * f; }, true);
    sweep("percussion: tension sweep is click-free",     [](MatterParams& p, double f) { p.tension = 0.8 + 0.8 * f; }, true);
    sweep("percussion: berger sweep is click-free",      [](MatterParams& p, double f) { p.bergerAmount = 0.0 + 1.0 * f; }, true);
    sweep("percussion: cavity tune sweep is click-free", [](MatterParams& p, double f) { p.cavityTune = 0.2 + 1.0 * f; }, true);
    sweep("percussion: cavity amount sweep is click-free", [](MatterParams& p, double f) { p.cavityAmount = 0.0 + 1.0 * f; }, true);
    sweep("percussion: damping tilt sweep is click-free", [](MatterParams& p, double f) { p.dampTilt = 0.0 + 1.0 * f; }, true);
    sweep("percussion: damping sweep is click-free",     [](MatterParams& p, double f) { p.damping = 0.0 + 0.5 * f; }, true);
    sweep("percussion: mode count sweep is click-free",  [](MatterParams& p, double f) { p.membraneModes = 2 + (int) (10.0 * f); }, true);
    sweep("percussion: wire amount sweep is click-free", [](MatterParams& p, double f) { p.wireAmount = 0.0 + 1.0 * f; }, true);
}

// ---- Test 1: zero-input decay ------------------------------------------------
static void testZeroInputDecay(double sr, int os)
{
    MatterVoice v;
    MatterParams fp;
    fp.memory = 0.0; fp.healing = 0.0; fp.agitation = 0.0;
    fp.contactHardness = 0.0; fp.contactLoss = 0.0;   // isolate the Hamiltonian core
    fp.damping = 0.2; fp.nonlinDamping = 0.15; fp.excitation = ExcitationMode::Pluck;
    v.prepare(sr);
    v.setOversampling(os);
    v.setMemoryStiffnessModulation(false);
    v.updateParameters(fp);
    v.seedFreeDecay(1.0, 0.0);

    const double H0 = v.storedEnergy();
    bool finite = true;
    double lastPeak = 0.0;
    const int N = (int) (3.0 * sr);
    for (int n = 0; n < N; ++n)
    {
        const float s = v.renderSample();
        if (!std::isfinite(s)) { finite = false; break; }
        if (n == N - 1) lastPeak = v.lastOutputPeak();
    }
    const double H1   = v.storedEnergy();
    const double rise = v.energyMonitor().maxUnforcedRise();

    check(finite, "zero-input decay: output finite (bounded states)");
    // The core admissibility statement: unforced H is monotonically non-increasing
    // (discrete H_{n+1} <= H_n + eps_num), to numerical precision.
    check(rise <= 1e-3 * std::fabs(H0) + 1e-6,
          "zero-input decay: unforced energy never grows (H_{n+1} <= H_n + eps)",
          "maxRise=" + std::to_string(rise));
    // Bounded below (does not diverge to -inf) and did not grow above the start.
    check(std::isfinite(H1) && H1 <= H0 * 1.0001 + 1.0 && H1 > -1.0e3 * (std::fabs(H0) + 1.0),
          "zero-input decay: energy bounded below and not growing",
          "H0=" + std::to_string(H0) + " H1=" + std::to_string(H1));
    // Motion actually ceases (the audible signal decays to silence).
    check(lastPeak < 1.0e-3, "zero-input decay: motion decays to silence",
          "finalPeak=" + std::to_string(lastPeak));
}

// ---- Test 2: no-force equilibrium -------------------------------------------
static void testEquilibrium(double sr, int os)
{
    MatterSynth synth;
    MatterParams p; p.oversampling = os;
    synth.prepare(sr, 512);
    synth.setParameters(p);
    std::vector<float> L(256), R(256);
    double peak = 0;
    for (int b = 0; b < 40; ++b)
    {
        synth.renderBlock(L.data(), R.data(), 256);
        for (float s : L) peak = std::max(peak, (double) std::fabs(s));
    }
    check(peak < 1e-9, "no-force equilibrium: silent at rest", "peak=" + std::to_string(peak));
}

// ---- Test 3: dissipation sign -----------------------------------------------
static void testDissipationSign(double sr, int os)
{
    const double dt = 1.0 / (sr * os);
    NonlinearResonator r; r.configure(7.6e6, -1.0e6, 5.0e5, 5.0, 0.001);
    bool ok = true;
    for (double p = -3000; p <= 3000; p += 137.0)
    {
        double pp = p;
        const double d = r.applyDamping(pp, dt);
        if (d < -1e-9) ok = false;
        if (std::fabs(pp) > std::fabs(p) + 1e-9) ok = false; // |p| never grows
    }
    check(ok, "dissipation sign: resonator damping removes energy for all sampled p");

    ContactModel c; c.configure(1.0e6, 5.0e4, 0.5);
    bool cok = true;
    for (double q = -1.0; q <= 2.0; q += 0.05)
        if (c.dampingCoeff(q) < -1e-12) cok = false;
    check(cok, "dissipation sign: contact damping coefficient nonnegative");

    CrackleNetwork net; net.configure(42, 0.6, 1.0, 0.2, 1.5e-3, 0.5);
    bool nok = true;
    for (int i = 0; i < 5000; ++i)
    {
        double ev = 0, diss = 0;
        net.step(0.8 * std::sin(i * 0.01), 0.3, dt, 0.5, ev, diss);
        if (diss < -1e-15) nok = false;
    }
    check(nok, "dissipation sign: crackle gradient-flow dissipation nonnegative");
}

// ---- Test 4: bounded stress --------------------------------------------------
static void testBoundedStress(double sr, int os)
{
    MatterParams p;
    p.tension = 4.0; p.nonlinStiffness = 1.0; p.hardening = 1.0; p.coupling = 1.0;
    p.brittleness = 1.0; p.adhesion = 1.0; p.roughness = 1.0; p.crackleCoupling = 1.0;
    p.contactHardness = 1.0; p.contactLoss = 1.0; p.damping = 0.02; p.nonlinDamping = 0.02;
    p.excitation = ExcitationMode::LoadSnap; p.loadingRate = 1.0; p.snapDepth = 1.0;
    p.pluckEnergy = 1.0; p.memory = 1.0; p.agitation = 0.5; p.oversampling = os;

    MatterSynth synth; synth.prepare(sr, 512); synth.setParameters(p);
    const int total = (int) (30.0 * sr);
    const int block = 128;
    std::vector<float> L((size_t) block), R((size_t) block);
    bool finite = true;
    int n = 0;
    // hammer several notes over the 30 s.
    int nextNote = 0;
    while (n < total)
    {
        if (n >= nextNote) { synth.noteOn(48 + (n / (int) sr) % 24, 1.0); nextNote = n + (int) (0.5 * sr); }
        const int m = std::min(block, total - n);
        synth.renderBlock(L.data(), R.data(), m);
        for (int i = 0; i < m; ++i) if (!std::isfinite(L[(size_t) i])) { finite = false; break; }
        if (!finite) break;
        n += m;
    }
    check(finite, "bounded stress: 30 s at extreme params stays finite",
          "resets=" + std::to_string(synth.totalResets()));
}

// ---- Test 5: sample-rate consistency ----------------------------------------
static void testSampleRateConsistency(int os)
{
    RenderStats s44, s96;
    renderNote(presetStableNonlinearBell(), 60, 0.9, 44100.0, os, 1.5, 1.2, &s44);
    renderNote(presetStableNonlinearBell(), 60, 0.9, 96000.0, os, 1.5, 1.2, &s96);
    const double fr = (s44.domFreq > 0 && s96.domFreq > 0)
                        ? std::fabs(s44.domFreq - s96.domFreq) / s96.domFreq : 1.0;
    const double rr = (s96.rms > 1e-9) ? s44.rms / s96.rms : 0.0;
    check(fr < 0.05, "sample-rate: dominant frequency within 5%",
          "44k=" + std::to_string(s44.domFreq) + " 96k=" + std::to_string(s96.domFreq));
    check(rr > 0.4 && rr < 2.5, "sample-rate: RMS envelope comparable",
          "ratio=" + std::to_string(rr));
}

// ---- Test 6: determinism -----------------------------------------------------
static void testDeterminism(double sr, int os)
{
    auto a = renderNote(presetAvalancheCrackle(), 50, 0.8, sr, os, 1.5, 1.0);
    auto b = renderNote(presetAvalancheCrackle(), 50, 0.8, sr, os, 1.5, 1.0);
    bool identical = (a.size() == b.size());
    if (identical)
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i]) { identical = false; break; }
    check(identical, "determinism: same seed/params -> bit-identical output");
}

// ---- Test 6b: determinism across host buffer sizes ---------------------------
// The shared material-memory state must be folded per-sample, not per-block, or a
// two-note sequence renders differently at different buffer sizes (regression for
// the review finding). Uses a memory-active preset and two sequential notes.
static void testDeterminismBlockSize(double sr, int os)
{
    auto renderTwoNotes = [&](int block) -> std::vector<float>
    {
        MatterParams p = presetCreakingJoint();  // memory = 0.7
        p.oversampling = os;
        MatterSynth synth; synth.prepare(sr, block); synth.setParameters(p);
        const int total = (int) (2.0 * sr);
        std::vector<float> out((size_t) total, 0.0f), L((size_t) block), R((size_t) block);
        const int n1On = 0, n1Off = (int) (0.4 * sr);
        const int n2On = (int) (0.8 * sr), n2Off = (int) (1.2 * sr);
        for (int n = 0; n < total; )
        {
            if (n == n1On)  synth.noteOn(50, 0.8);
            if (n == n1Off) synth.noteOff(50);
            if (n == n2On)  synth.noteOn(50, 0.8);
            if (n == n2Off) synth.noteOff(50);
            const int m = std::min(block, total - n);
            synth.renderBlock(L.data(), R.data(), m);
            for (int i = 0; i < m; ++i) out[(size_t)(n + i)] = L[(size_t) i];
            n += m;
        }
        return out;
    };
    auto a = renderTwoNotes(32);
    auto b = renderTwoNotes(480);
    bool identical = (a.size() == b.size());
    double maxDiff = 0.0;
    if (identical)
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double d = std::fabs((double) a[i] - (double) b[i]);
            if (d > maxDiff) maxDiff = d;
            if (a[i] != b[i]) identical = false;
        }
    check(identical, "determinism: identical across host buffer sizes (32 vs 480)",
          "maxDiff=" + std::to_string(maxDiff));
}

// ---- Test 7: event emergence -------------------------------------------------
static void testEventEmergence(double sr, int os)
{
    const double dt = 1.0 / (sr * os);
    // Bistable well: a=1, b=1 => minima near +-1, saddle-node tilt lambda_c ~ 0.385.
    auto rampAndFlip = [&](double lambdaMax) -> bool
    {
        BistableElement e; e.configure(1.0, 1.0, 1.0e-3);
        double s = -1.0;                       // seated in the left well
        bool flipped = false;
        const int steps = (int) (0.5 * sr * os);
        for (int i = 0; i < steps; ++i)
        {
            e.lambda = lambdaMax * (double) i / (double) steps; // slow ramp
            e.step(s, 0.0, dt, 0.5);
            if (s > 0.5) flipped = true;
        }
        return flipped;
    };
    check(rampAndFlip(1.0),  "event emergence: bistable snaps when loaded past the barrier");
    check(!rampAndFlip(0.2), "event emergence: bistable does NOT snap below threshold");
}

// ---- Test 8: silence after decay --------------------------------------------
static void testSilenceAfterDecay(double sr, int os)
{
    MatterParams p = presetPlasticPop(); p.oversampling = os;
    MatterSynth synth; synth.prepare(sr, 512); synth.setParameters(p);
    std::vector<float> L(128), R(128);
    synth.noteOn(60, 0.9);
    int n = 0; const int relAt = (int) (0.3 * sr); const int maxN = (int) (8.0 * sr);
    bool released = false, slept = false;
    while (n < maxN)
    {
        if (!released && n >= relAt) { synth.noteOff(60); released = true; }
        synth.renderBlock(L.data(), R.data(), 128);
        n += 128;
        if (released && synth.activeVoices() == 0) { slept = true; break; }
    }
    check(slept, "silence after decay: voice sleeps and releases CPU",
          "t=" + std::to_string((double) n / sr) + "s");
}

// =============================================================================
//  v2 tests
// =============================================================================

// ---- Test 9: percussion unforced-energy admissibility -------------------------
// PERCUSSION_SPEC.md: forcing off, alpha / kappa / orifice / wires ALL active,
// maxUnforcedRise must stay at numerical epsilon.
static void testPercussionEnergy(double sr)
{
    MatterVoice v;
    MatterParams p;
    p.engine = EngineMode::Percussion;
    p.membraneModes = 16;
    p.bergerAmount = 0.9;    // strong Berger quartic
    p.cavityAmount = 0.8;    // strong (PD-clamped) Helmholtz coupling
    p.cavityTune = 0.5; p.cavityLoss = 0.7;    // orifice quadratic drag on
    p.wireAmount = 0.6;      // wire contact bank engaged
    p.dampTilt = 0.4; p.damping = 0.15; p.oversampling = 8;
    v.prepare(sr);
    v.setOversampling(8);
    v.updateParameters(p);
    v.seedFreeDecay(0.8, -0.35);   // finite modal energy, NO mallet, NO forcing

    const double H0 = v.storedEnergy();
    bool finite = true;
    const int N = (int) (2.0 * sr);
    for (int n = 0; n < N; ++n)
        if (!std::isfinite(v.renderSample())) { finite = false; break; }
    const double H1   = v.storedEnergy();
    const double rise = v.energyMonitor().maxUnforcedRise();

    check(finite, "percussion: unforced decay stays finite");
    check(rise <= 1e-3 * std::fabs(H0) + 1e-6,
          "percussion: unforced energy never grows (Berger+cavity+orifice+wires)",
          "maxRise=" + std::to_string(rise) + " H0=" + std::to_string(H0));
    check(std::isfinite(H1) && H1 <= H0 * 1.0001 + 1.0,
          "percussion: energy decays overall",
          "H0=" + std::to_string(H0) + " H1=" + std::to_string(H1));
}

// ---- Test 10: percussion determinism + preset sanity -------------------------
static void testPercussionPresets(double sr)
{
    auto a = renderNote(presetMembraneKick(), 36, 0.9, sr, 4, 1.2, 0.3);
    auto b = renderNote(presetMembraneKick(), 36, 0.9, sr, 4, 1.2, 0.3);
    bool identical = (a.size() == b.size());
    if (identical)
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i]) { identical = false; break; }
    check(identical, "percussion: kick render is bit-deterministic");

    const char* names[4] = { "Membrane Kick", "Cavern Kick", "Wire Snare", "Loose Rattle" };
    MatterParams ps[4] = { presetMembraneKick(), presetCavernKick(),
                           presetWireSnare(), presetLooseRattle() };
    for (int i = 0; i < 4; ++i)
    {
        RenderStats st;
        renderNote(ps[i], i < 2 ? 36 : 50, 0.9, sr, ps[i].oversampling, 1.2, 0.3, &st);
        const bool ok = std::isfinite(st.peak) && st.peak > 1.0e-4 && st.peak < 1.4
                        && st.resets == 0;
        check(ok, (std::string("percussion preset sane: ") + names[i]).c_str(),
              "peak=" + std::to_string(st.peak) + " resets=" + std::to_string(st.resets));
    }
}

// ---- Test 11: per-voice AGC --------------------------------------------------
static void testAgc(double sr)
{
    MatterParams hot = presetPlasticPop();
    hot.output = 1.0; hot.pluckEnergy = 1.0; hot.velocitySens = 0.0;
    hot.agc = 1.0;
    RenderStats on;
    renderNote(hot, 60, 1.0, sr, 4, 1.0, 0.4, &on);
    hot.agc = 0.0;
    RenderStats off;
    renderNote(hot, 60, 1.0, sr, 4, 1.0, 0.4, &off);
    // With AGC fully on, per-voice peaks sit at the AGC target — below the
    // limiter knee (0.8) — so preset attacks never touch the emergency limiter.
    check(on.peak <= cfg::kAgcTarget + 0.02,
          "AGC: attack peak held at target (limiter untouched)",
          "peakOn=" + std::to_string(on.peak) + " peakOff=" + std::to_string(off.peak));
    check(on.peak <= off.peak + 1e-9, "AGC: gain is downward-only");
}

// ---- Test 12: LFO shapes, determinism, sync math -----------------------------
static void testLfo(double sr)
{
    using namespace acv2;
    bool bounded = true;
    for (int s = 0; s < kNumLfoShapes; ++s)
    {
        Lfo l;
        l.prepare(sr);
        l.setShape((LfoShape) s);
        l.setRateHz(3.7);
        l.reseed(0xABCDEF1234ull);
        for (int n = 0; n < 20000; ++n)
        {
            const float v = l.processSample();
            if (!(v >= -1.0f && v <= 1.0f)) { bounded = false; break; }
        }
    }
    check(bounded, "LFO: all 8 shapes bounded in [-1,1]");

    // determinism: same seed => identical random-shape streams
    Lfo a, b;
    a.prepare(sr); b.prepare(sr);
    a.setShape(LfoShape::SmoothRandom); b.setShape(LfoShape::SmoothRandom);
    a.setRateHz(11.0); b.setRateHz(11.0);
    a.reseed(42);      b.reseed(42);
    bool identical = true;
    for (int n = 0; n < 10000; ++n)
        if (a.processSample() != b.processSample()) { identical = false; break; }
    check(identical, "LFO: same seed -> bit-identical random stream");

    // synced rate follows the frozen division table
    Lfo s;
    s.prepare(sr);
    s.setBpm(120.0);
    s.setSyncedDivision(8);            // "1/4" -> 500 ms -> 2 Hz
    const bool syncOk = std::fabs(s.rateHz() - 2.0) < 1e-9;
    check(syncOk, "LFO: synced 1/4 @ 120 BPM = 2 Hz",
          "hz=" + std::to_string(s.rateHz()));
}

// ---- Test 13: seed policy ----------------------------------------------------
static void testSeedPolicy()
{
    using namespace acv2;
    SeedPolicy sp;
    sp.setGlobalSeed(1234);

    sp.setMode(SeedMode::PerKey);
    const auto k60a = sp.onNoteOn(60);
    const auto k61  = sp.onNoteOn(61);
    const auto k60b = sp.onNoteOn(60);
    check(k60a == k60b && k60a != k61,
          "seed policy: Per-Key is stable per key, distinct across keys");

    sp.setMode(SeedMode::Fixed);
    check(sp.onNoteOn(10) == sp.onNoteOn(99),
          "seed policy: Fixed ignores the key");

    sp.setMode(SeedMode::RandomOnStrike);
    const auto r1 = sp.onNoteOn(60);
    const auto r2 = sp.onNoteOn(60);
    check(r1 != r2 && sp.lastUsedSeed() == r2,
          "seed policy: Random-on-strike differs per strike and reports back");

    // and the voice reproduces from a locked seed: same noteSeed => same render
    MatterParams p = presetAvalancheCrackle();
    p.noteSeed = r2;
    auto x = renderNote(p, 50, 0.8, 48000.0, 4, 0.8, 0.5);
    auto y = renderNote(p, 50, 0.8, 48000.0, 4, 0.8, 0.5);
    bool identical = x.size() == y.size();
    if (identical)
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i] != y[i]) { identical = false; break; }
    check(identical, "seed policy: locked noteSeed -> bit-identical voice");
}

// ---- Test 14: tempo-sync division table + units round-trips ------------------
static void testTempoAndUnits()
{
    using namespace acv2;
    bool ok = tempo::kNumDivisions == 16
           && std::fabs(tempo::divisionToMs( 8, 120.0) - 500.0)    < 1e-9   // 1/4
           && std::fabs(tempo::divisionToMs( 5, 120.0) - 250.0)    < 1e-9   // 1/8
           && std::fabs(tempo::divisionToMs( 3, 120.0) - 250.0*2/3.0) < 1e-9 // 1/8T
           && std::fabs(tempo::divisionToMs( 4, 120.0) - 187.5)    < 1e-9   // 1/16.
           && std::fabs(tempo::divisionToMs(15, 120.0) - 8000.0)   < 1e-9;  // 4/1
    check(ok, "tempo: frozen division table math (120 BPM)");

    ok = std::fabs(tempo::msToHz(250.0) - 4.0) < 1e-12
      && std::fabs(tempo::hzToMs(4.0) - 250.0) < 1e-12;
    check(ok, "tempo: ms <-> Hz");

    ok = std::fabs(units::textToMs(units::msToText(437.5)) - 438.0)  < 1.0   // %.0f >= 100ms
      && std::fabs(units::textToMs("1.25 s") - 1250.0)  < 1e-9
      && std::fabs(units::textToHz("2.5 kHz") - 2500.0) < 1e-9
      && units::noteNameToMidi(units::noteName(60).c_str()) == 60
      && std::fabs(units::textToPercent("+35 %") - 0.35) < 1e-12;
    check(ok, "units: ms / Hz / note / percent round-trips");
}

// -----------------------------------------------------------------------------
static void renderAllPresets(const std::string& outDir, double sr, int os)
{
    std::printf("\nRendering presets (%.0f Hz, %dx oversampling) to %s\n", sr, os, outDir.c_str());
    std::printf("  %-28s %8s %8s %10s %7s\n", "preset", "peak", "rms", "domFreq", "resets");
    auto presets = factoryPresets();
    for (auto& pr : presets)
    {
        RenderStats st;
        // Load/Snap and crackle presets get a longer hold; plucks a short one.
        const bool sustained = (pr.params.excitation == ExcitationMode::LoadSnap);
        const double total = sustained ? 3.0 : 2.2;
        const double rel   = sustained ? 1.8 : 0.4;
        auto buf = renderNote(pr.params, 60, 0.9, sr, os, total, rel, &st);

        std::string fname = outDir + "/";
        for (const char* c = pr.name; *c; ++c) fname += (*c == ' ') ? '_' : *c;
        fname += ".wav";
        writeWavMono16(fname, buf, (int) sr);

        std::printf("  %-28s %8.4f %8.4f %10.2f %7d\n",
                    pr.name, st.peak, st.rms, st.domFreq, st.resets);
    }
}

// -----------------------------------------------------------------------------
int main(int argc, char** argv)
{
    std::string outDir = "renders";
    double sr = 48000.0;
    int    os = 4;
    bool   doTest = true, doRender = true;

    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outDir = argv[++i];
        else if (!std::strcmp(argv[i], "--sr") && i + 1 < argc) sr = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--os") && i + 1 < argc) os = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--test-only")) doRender = false;
        else if (!std::strcmp(argv[i], "--render-only")) doTest = false;
    }

    std::printf("MatterTone POC — offline harness\n");
    std::printf("sample rate = %.0f Hz, oversampling = %dx\n", sr, os);

    if (doTest)
    {
        std::printf("\n== DSP core tests ==\n");
        testZeroInputDecay(sr, os);
        testEquilibrium(sr, os);
        testDissipationSign(sr, os);
        testBoundedStress(sr, os);
        testSampleRateConsistency(os);
        testDeterminism(sr, os);
        testDeterminismBlockSize(sr, os);
        testEventEmergence(sr, os);
        testSilenceAfterDecay(sr, os);

        std::printf("\n== v2 tests ==\n");
        testPercussionEnergy(sr);
        testPercussionPresets(sr);
        testAgc(sr);
        testLfo(sr);
        testSeedPolicy();
        testTempoAndUnits();
        testParameterSmoothing(sr, os);
        std::printf("\n%d test(s) failed.\n", failures);
    }

    if (doRender)
        renderAllPresets(outDir, sr, os);

    return failures;
}
