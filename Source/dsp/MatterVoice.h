// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  MatterTone POC — MatterVoice.h
//  One polyphonic voice: a driven, dissipative, finite-dimensional nonlinear
//  system integrated by operator splitting, whose radiated signal IS the sound.
//
//  State vector  z = (q1, p1, q2, p2, xi, sSnap, s_1..s_N):
//    (q1,p1),(q2,p2) : two coupled nonlinear resonant modes (unit mass)
//    xi in [0,1]     : slow material-memory variable
//    sSnap           : primary bistable "snap" element
//    s_1..s_N        : heterogeneous crackle network (in CrackleNetwork)
//
//  Non-dimensionalisation: the snap/crackle/contact subsystems are written at
//  UNIT stiffness scale, and their coupling back into the audible mode is scaled
//  by S = k1 (the resonator stiffness) so all forces are commensurate while the
//  bistable dynamics keep O(1) barriers. This is what lets a "small" snap element
//  audibly kick a stiff resonator without destabilising either. See MODEL.md.
//
//  Energy admissibility (unforced): with J antisymmetric (conservative exchange
//  integrated symplectically) and R >= 0 (every damping channel is a dissipative
//  sub-step), dH/dt = -grad H^T R grad H <= 0. The EnergyMonitor verifies the
//  discrete analogue H_{n+1} <= H_n + eps_num.
//
//  Pure C++17, NO JUCE dependency.
// =============================================================================
#pragma once

#include <cstdint>
#include "Config.h"
#include "MatterParams.h"
#include "NonlinearResonator.h"
#include "BistableElement.h"
#include "ContactModel.h"
#include "CrackleNetwork.h"
#include "PercussionEngine.h"
#include "Oversampler.h"
#include "EnergyMonitor.h"
#include "Rng.h"

namespace matter
{
class MatterVoice
{
public:
    MatterVoice() = default;

    void  prepare(double sampleRate);
    void  setOversampling(int factor);   // 2 / 4 / 8

    void  noteOn(int midiNote, double velocity01, const MatterParams& params);
    void  noteOff();
    void  updateParameters(const MatterParams& params); // live edits on a held voice

    void  setPitchBendSemitones(double semis) { pitchBendSemis = semis; }
    void  setModWheel(double v01)             { modWheel = v01; }
    void  setAftertouch(double v01)           { aftertouch = v01; }

    // Advance one OUTPUT sample (internally 'os' sub-steps) and return the mono
    // radiated sample. Real-time safe: no allocations / locks / IO / exceptions.
    float renderSample();

    bool  isActive()  const { return active; }
    int   midiNote()  const { return note; }
    bool  isReleased() const { return released; }

    // Give way to a new note: fade to silence over `seconds` (2 ms: sudden to
    // the ear, no step in the waveform), then free the voice. A fading voice
    // no longer counts toward the polyphony.
    void  kill(double seconds = 0.002)
    {
        if (!active || fading) return;
        fading = true;
        killing = true;                // keep integrating: only a blow-up freezes
        const int n = (int) (seconds * sampleRate);
        fadeLen = fadeCounter = n > 1 ? n : 1;
    }
    bool  isFading() const { return fading; }

    void  reset();   // hard reset state to silent equilibrium
    void  panic()    { reset(); active = false; }

    // Persistent material memory: a voice starts a note at this xi (set by the
    // synth from the shared material state) so repeated notes differ by history.
    void  setInitialMemory(double m) { initialMemory = (m < 0.0 ? 0.0 : (m > 1.0 ? 1.0 : m)); }
    void  healMemory()               { xi = 1.0; }

    // Diagnostics (UI / tests).
    double storedEnergy() const { return monitor.storedEnergy(); }
    double memoryValue()  const { return xi; }
    int    resets()       const { return resetCount; }
    double lastOutputPeak() const { return envPeak; }
    const EnergyMonitor& energyMonitor() const { return monitor; }

    // Expose the enable flag for the strict unforced-energy tests: when false,
    // memory does NOT parametrically modulate stiffness (keeps H time-invariant).
    void setMemoryStiffnessModulation(bool on) { memoryModStiffness = on; }
    // Test hook: seed the voice with finite energy but no external forcing.
    // Engine-aware: with p.engine == Percussion it seeds the membrane modes
    // (mallet OFF) so the percussion unforced-energy harness can run.
    void seedFreeDecay(double q1init, double p1init);

private:
    // ---- setup / coefficient computation --------------------------------------
    void computeDesign();        // note/param-dependent base coefficients (ratios)
    void refreshDynamic();       // cheap per-sample scale (bend, mod, memory)
    void integrateSubstep(double dt, double& yOut, bool& blewUp, bool forced);
    void integrateSubstepPerc(double dt, double& yOut, bool& blewUp);
    void configurePercussion();  // (re)configure the percussion engine (note-on / edits)
    void conservativeForces(double q1_, double q2_, double& F1, double& F2) const;
    double totalEnergy() const;

    // ---- fixed environment ----------------------------------------------------
    double sampleRate = 48000.0;
    int    os         = 4;
    double dt         = 1.0 / (48000.0 * 4.0);
    Decimator decimator;

    // ---- parameters snapshot --------------------------------------------------
    MatterParams p_;
    int    note      = 60;
    double velocity  = 1.0;
    double pitchBendSemis = 0.0;
    double modWheel  = 0.0;
    double aftertouch = 0.0;

    // ---- state ----------------------------------------------------------------
    double q1 = 0, p1 = 0, q2 = 0, p2 = 0;
    double xi = 1.0;         // start "intact"
    double sSnap = 0.0;
    CrackleNetwork crackle;

    // ---- v2 percussion engine (EngineMode::Percussion) ------------------------
    PercussionEngine perc;
    bool   percConfigured = false;
    double percF0   = 60.0;          // membrane fundamental for this note (Hz)
    std::uint64_t percSeed = 1;      // wire-bank heterogeneity seed
    double percOutScale = 0.5;       // radiation normalisation for the perc engine

    // ---- design coefficients (set in computeDesign, unit/base scale) ----------
    double k1_0 = 1, k2_0 = 1;              // base stiffnesses (before bend/memory)
    double betaRatio1 = 0, betaRatio2 = 0;  // beta_i / k_i
    double deltaRatio1 = 0, deltaRatio2 = 0;// delta_i / k_i
    double c1_ = 0, c2_ = 0;                // linear damping (abs)
    double c3n1 = 0, c3n2 = 0;              // nonlinear damping (normalised by omega)
    double gammaRatio = 0;                  // gamma / k1
    double gsnSnap = 0;                     // snap<->q1 coupling (unit)
    double tauSnap = 1e-3;                  // snap relaxation time
    double snapBarrier0 = 1.0;              // base b for snap
    double kcRatio = 0;                     // contact kC / S
    double ccAbs   = 0;                     // contact damping coeff scale
    double contactWall = 1.0;
    double kHeal = 0, kBreak = 0, memStrength = 0;
    double wq = 0.15, wv = 0.55, wa = 0.30, wc = 0.4, ws = 0.6, wD = 0.1;
    double eventScale = 1.0, contactRadScale = 1.0, dissRadScale = 1.0;
    double outScale = 1.0;                  // per-voice amplitude normalisation

    // ---- dynamic (per-sample) scaled coefficients -----------------------------
    NonlinearResonator res1, res2;
    BistableElement    snap;
    ContactModel       contact;
    double S = 1.0;                         // = current k1 (resonator stiffness scale)
    double omegaRef = 1.0;
    double gammaCur = 0.0;                  // current intermodal coupling (S-scaled)
    double agitAmp  = 0.0;                  // bounded agitation force amplitude
    double loadMax  = 0.0;                  // max snap tilt for Load/Snap ramp

    // ---- excitation / loading -------------------------------------------------
    bool   released = false;
    double lambdaCur = 0.0;                 // current snap tilt (Load/Snap ramp)
    double loadBias  = 0.0;                 // crackle uniform loading
    double loadRateHz = 0.0;                // ramp rate
    double attackEnv = 1.0;                 // onset ramp 0->1 (de-click / soft attack)
    double attackInc = 1.0;                 // per-sample ramp increment

    // ---- lifecycle / safety ---------------------------------------------------
    bool   active = false;
    bool   fading = false;
    bool   killing = false;                 // the fade is a note giving way, not a blow-up
    int    fadeCounter = 0;
    int    fadeLen = cfg::kFadeOutSamples;   // length of the fade in progress
    int    resetCount = 0;
    double envPeak = 0.0;                   // output envelope follower
    int    silenceCounter = 0;
    double Href = 1.0;                      // frequency-scaled energy reference
    bool   memoryModStiffness = true;
    double initialMemory = 1.0;             // xi value a new note starts from

    // ---- v2 per-voice AGC (attacks never touch the emergency limiter) --------
    double agcPeak = 0.0;            // fast pre-limiter peak follower
    double agcRelCoeff = 0.9999;     // per-sample release coefficient

    // ---- DC block / safety high-pass on the per-voice output ------------------
    double dcX1 = 0, dcY1 = 0, dcR = 0.999;
    double hpX1 = 0, hpY1 = 0, hpA = 0.0;

    EnergyMonitor monitor;
    Rng           agitationRng { 1 };
};
} // namespace matter
