// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  MatterTone POC — MatterParams.h
//  The clean seam between the host/JUCE layer and the pure DSP core.
//  The JUCE AudioProcessorValueTreeState fills this struct once per block; the
//  offline render/test harness fills it directly. The DSP core depends ONLY on
//  this struct, never on JUCE.
//
//  Values here are already de-normalised into engineering ranges (not 0..1),
//  so the mapping from user-facing normalised parameters lives in ONE place
//  (the JUCE processor / the harness) and the physics reads meaningful numbers.
// =============================================================================
#pragma once

#include <cstdint>

namespace matter
{
enum class ExcitationMode { Pluck = 0, LoadSnap = 1 };

enum class TuningMode { ChromaticReference = 0, RatioSet = 1, AttractorScale = 2 };

// v2: which physical model a voice integrates.
enum class EngineMode { Modal = 0, Percussion = 1 };

// v2: how the per-note seed is derived (acv2::SeedPolicy semantics).
enum class SeedModeV2 { Fixed = 0, PerKey = 1, RandomOnStrike = 2 };

// All controls consumed by the DSP core. Defaults give a stable, musically useful
// "rubber-ish" plucked tone. Ranges are documented in MODEL.md and clamped by the
// core (clampToSafeRanges) so no host automation value can produce NaNs.
struct MatterParams
{
    // ---- Global ----------------------------------------------------------------
    double output          = 0.7;    // linear output gain (0..2)
    int    numVoices       = 8;      // 1..kMaxVoices
    int    oversampling    = 4;      // 2 / 4 / 8
    unsigned seed          = 1;      // deterministic material seed
    TuningMode tuningMode  = TuningMode::ChromaticReference;
    int    rootNote        = 60;     // MIDI root for ratio / attractor modes
    double bodyMix         = 0.25;   // dry/body-filter blend (0..1)
    double eventMix        = 0.5;    // weight of contact/snap/crackle in radiation (0..1)

    // ---- Material --------------------------------------------------------------
    double tension         = 1.0;    // scales baseline stiffness k1     (0.25..4)
    double nonlinStiffness = 0.35;   // magnitude of cubic/quintic terms (0..1)
    double hardening       = 0.4;    // -1 softening .. +1 hardening (sign of beta)
    double damping         = 0.15;   // linear modal damping c_i         (0..1)
    double nonlinDamping   = 0.08;   // |v|v damping c3_i                (0..1)
    double coupling        = 0.25;   // intermodal coupling gamma        (0..1)
    double brittleness     = 0.4;    // snap/crackle barrier sharpness   (0..1)
    double adhesion        = 0.3;    // contact/crackle stickiness       (0..1)
    double roughness       = 0.5;    // crackle heterogeneity spread     (0..1)
    double healing         = 0.3;    // memory recovery rate k_heal      (0..1)
    double memory          = 0.4;    // strength of xi modulation        (0..1)
    double contactHardness = 0.3;    // Hertz stiffness k_c              (0..1)
    double contactLoss     = 0.3;    // contact damping c_c              (0..1)
    double crackleCoupling = 0.3;    // network coupling K_c (pops<->cascades) (0..1)

    // ---- Excitation ------------------------------------------------------------
    ExcitationMode excitation = ExcitationMode::Pluck;
    double attack          = 0.1;    // onset ramp (de-click / soft attack) (0..1)
    double pluckEnergy     = 0.5;    // injected pluck displacement/energy (0..1)
    double loadingRate     = 0.4;    // snap-loading ramp rate            (0..1)
    double snapDepth       = 0.5;    // bistable well depth b             (0..1)
    double strikePosition  = 0.3;    // modal balance mode1<->mode2       (0..1)
    double velocitySens    = 0.7;    // how much MIDI velocity scales energy (0..1)
    double agitation       = 0.0;    // bounded seeded stochastic forcing (0..1)

    // ---- Tone / Scale ----------------------------------------------------------
    double modeRatio       = 1.5;    // f2/f1 ratio (Ratio Set uses table instead)
    int    attractorSet    = 0;      // which parameter-state bank
    double scaleSpread     = 1.0;    // spreads ratio-set / attractor detune
    double energyToPitch   = 0.3;    // how much excitation energy detunes pitch (0..1)
    double stabilityBias   = 0.5;    // pushes attractors toward stable pitched tone (0..1)

    // =========================================================================
    //  v2 additions (all default to v1-equivalent behaviour)
    // =========================================================================
    // ---- Engine ----------------------------------------------------------------
    EngineMode engine      = EngineMode::Modal;
    int    membraneModes   = 6;      // percussion mode count (2..kMaxMembraneModes)
    double bergerAmount    = 0.5;    // Berger tension nonlinearity (pitch-glide thump) (0..1)
    double cavityAmount    = 0.4;    // Helmholtz cavity coupling kappa (PD-clamped) (0..1)
    double cavityTune      = 0.45;   // omega_H / omega_0 ratio                  (0.1..1.5)
    double cavityLoss      = 0.4;    // orifice quadratic drag on p_H            (0..1)
    double wireAmount      = 0.0;    // snare-wire contact bank engagement       (0..1)
    double dampTilt        = 0.3;    // Rayleigh high-mode damping (b * omega^2) (0..1)
    double malletHardness  = 0.6;    // mallet Hertz contact stiffness           (0..1)

    // ---- v2 time overrides (JUCE layer sets these from the ms/beats trios; the
    //      0 default preserves the v1 normalised-parameter mappings) -------------
    double attackMsOverride   = 0.0; // > 0: onset ramp time in ms (0.3..1000)
    double loadTimeMsOverride = 0.0; // > 0: time to full snap-load in ms (20..10000)

    // ---- v2 seed policy --------------------------------------------------------
    SeedModeV2 seedMode    = SeedModeV2::PerKey;  // v1 behaviour = PerKey
    // When nonzero, MatterVoice uses this 64-bit seed directly instead of
    // hashing (seed, note) — set by MatterSynth's SeedPolicy per note-on.
    std::uint64_t noteSeed = 0;

    // ---- v2 per-voice AGC ------------------------------------------------------
    double agc             = 1.0;    // 0 = off, 1 = full (attacks never hit limiter)
};
} // namespace matter
