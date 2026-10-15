// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  MatterTone POC — Config.h
//  Single home for hard limits, structural constants, and safety thresholds.
//  Rule (from brief): every hard limit lives here, no hidden magic numbers.
//  This header is pure C++17 and has NO JUCE dependency.
// =============================================================================
#pragma once

#include <cstddef>

namespace matter
{
// -----------------------------------------------------------------------------
//  Structural sizes
// -----------------------------------------------------------------------------
namespace cfg
{
    // Number of heterogeneous bistable elements in the crackle network.
    // Brief suggests N = 8 or 16; profile before increasing.
    inline constexpr int   kNumCrackle       = 16;

    // Maximum polyphony (voices). Global param clamps the active count 1..kMaxVoices.
    inline constexpr int   kMaxVoices        = 8;

    // Internal oversampling factors selectable by the Oversampling parameter.
    // The nonlinear ODE is integrated at sampleRate * factor.
    inline constexpr int   kOversampleMin    = 2;
    inline constexpr int   kOversampleMax    = 8;

    // Optional extra local substeps taken *near* contact / snap events, on top of
    // the global oversampling. Capped so real-time cost stays predictable.
    inline constexpr int   kMaxEventSubsteps = 4;

    // -------------------------------------------------------------------------
    //  Numerical safety thresholds (per voice)
    // -------------------------------------------------------------------------
    // A voice is declared "blown up" and reset if any of these trip. The energy
    // reference is frequency-scaled at note-on (see MatterVoice), so the cap is a
    // dimensionless multiple of the note's characteristic stiffness energy.
    inline constexpr double kEnergyBlowupFactor = 1.0e6;   // H > factor * Href  -> reset
    inline constexpr double kMaxDisplacement    = 200.0;   // |q_i| bound (scaled units)
    inline constexpr double kMaxCrackleState    = 20.0;    // |s_j| bound
    inline constexpr double kMaxMomentumScale   = 400.0;   // |p_i| bound is this * omegaRef

    // Number of samples over which a failing voice is faded to zero before reset.
    inline constexpr int    kFadeOutSamples     = 32;

    // -------------------------------------------------------------------------
    //  Voice lifecycle
    // -------------------------------------------------------------------------
    // A voice goes to sleep (releases CPU) once its output proxy stays below this
    // RMS-ish level for kSilenceHoldSamples consecutive samples after note-off.
    inline constexpr double kSilenceThreshold   = 1.0e-5;
    inline constexpr int    kSilenceHoldSamples = 2048;

    // Denormal flush threshold (belt-and-suspenders alongside FTZ/DAZ).
    inline constexpr double kDenormalFloor      = 1.0e-30;

    // -------------------------------------------------------------------------
    //  Regularisation widths (keep nonlinearities differentiable)
    // -------------------------------------------------------------------------
    // Smooth positive-part width for the unilateral contact (units of displacement).
    inline constexpr double kContactSmooth      = 1.0e-3;
    // Floor added under sqrt() for the dissipation-rate radiation term.
    inline constexpr double kDissipationEps     = 1.0e-12;

    // -------------------------------------------------------------------------
    //  Fixed physical scales (bounded-below potential guarantees)
    // -------------------------------------------------------------------------
    // Highest-order (sextic) coefficient is ALWAYS strictly positive so H is
    // bounded below even when the quartic term is softening (negative). See MODEL.md.
    inline constexpr double kSexticFloor        = 1.0;    // delta_i lower bound
    // Snap / crackle quartic coefficient a_j is always strictly positive.
    inline constexpr double kBistableQuartic    = 1.0;

    // Overdamped relaxation time-scale floor for internal variables (seconds-ish).
    inline constexpr double kEtaFloor           = 1.0e-4;

    // Audio safety high-pass corner (Hz) and DC-block corner (Hz).
    inline constexpr double kDCBlockHz          = 12.0;
    inline constexpr double kSafetyHPHz         = 18.0;

    // -------------------------------------------------------------------------
    //  v2 percussion engine (PERCUSSION_SPEC.md) — membrane + Berger + Helmholtz
    // -------------------------------------------------------------------------
    // Maximum number of membrane modes (kick ~6, snare ~16-24).
    inline constexpr int    kMaxMembraneModes   = 24;
    // Snare wire contact bank size (small bank of light one-sided contacts).
    inline constexpr int    kNumWires           = 8;
    // Berger tension clamp: cap the stiffening RATIO (1 + alpha*T/omega_i^2) at
    // this factor, IDENTICALLY in force and energy (head-tension saturation).
    inline constexpr double kBergerStiffenMax   = 6.0;
    // Helmholtz PD-margin: kappa <= this * the positive-definiteness bound.
    inline constexpr double kCavityPDMargin     = 0.9;
    // Symplectic stability cap omega_eff * dt for the percussion modes (matches
    // the modal engine's kOmegaDtMax, applied AFTER Berger max stiffening).
    inline constexpr double kPercOmegaDtMax     = 0.6;

    // -------------------------------------------------------------------------
    //  v2 per-voice AGC (attacks never touch the emergency limiter)
    // -------------------------------------------------------------------------
    inline constexpr double kAgcTarget          = 0.72;  // pre-limiter peak target
    inline constexpr double kAgcReleaseSecs     = 0.35;  // gain recovery time constant
}
} // namespace matter
