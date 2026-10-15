// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  MatterTone POC — AttractorScale.h
//  Three tuning strategies (see brief "Emergent tone and scale mode"):
//
//    * ChromaticReference — conventional 12-TET reference stiffness from f_ref.
//    * RatioSet           — the keyboard is an 8-degree scale of rational (or
//                           deliberately inharmonic) mode ratios about a root.
//    * AttractorScale      — each scale degree selects a *parameter state* of the
//                           nonlinear system (mode ratio, nonlinear stiffness,
//                           coupling, snap barrier, excitation energy). The root
//                           frequency is preserved and the OUTPUT pitch emerges
//                           from the resulting attractor.
//
//  No claim of cultural/historical authenticity is made for any tuning.
//  Header-only, pure C++17, no JUCE.
// =============================================================================
#pragma once

#include <cmath>
#include "MatterParams.h"

namespace matter
{
// A parameter-state perturbation applied by Attractor Scale mode (multiplicative
// about the patch's base values, except 'detune' which is a stiffness multiplier).
struct AttractorState
{
    double modeRatio       = 1.5;
    double nonlinStiffMul  = 1.0;
    double couplingMul     = 1.0;
    double snapBarrierMul  = 1.0;
    double energyMul       = 1.0;
    double stiffnessMul    = 1.0; // multiplies k1 => shifts the emergent pitch
};

class AttractorScale
{
public:
    static constexpr int kDegrees = 8;

    // ---- Rational / inharmonic ratio tables (one octave, 8 degrees) -----------
    // Familiar just-intonation set:  1/1 9/8 5/4 4/3 3/2 5/3 7/4 2/1
    static double justRatio(int degree)
    {
        static const double t[kDegrees] =
            { 1.0, 9.0/8.0, 5.0/4.0, 4.0/3.0, 3.0/2.0, 5.0/3.0, 7.0/4.0, 2.0 };
        return t[wrap(degree)];
    }

    // Deliberately unfamiliar set built from inharmonic (bell/plate-like) mode
    // ratios — NOT a conventional scale.
    static double unfamiliarRatio(int degree)
    {
        static const double t[kDegrees] =
            { 1.0, 1.1892, 1.4142, 1.6818, 2.0, 2.3784, 2.6651, 2.8284 };
        return t[wrap(degree)];
    }

    // Base frequency (Hz) used to set the note's baseline stiffness.
    //   note, rootNote : MIDI numbers.  a4 = 440.
    static double baseFrequency(const MatterParams& p, int note)
    {
        const double f_ref = 440.0 * std::pow(2.0, (note - 69) / 12.0);

        switch (p.tuningMode)
        {
            case TuningMode::ChromaticReference:
                return f_ref;

            case TuningMode::RatioSet:
            {
                const int    rel     = note - p.rootNote;
                const int    octave  = floorDiv(rel, kDegrees);
                const int    degree  = wrap(rel);
                const double rootF   = 440.0 * std::pow(2.0, (p.rootNote - 69) / 12.0);
                double ratio = (p.attractorSet % 2 == 0) ? justRatio(degree)
                                                         : unfamiliarRatio(degree);
                // scaleSpread stretches/compresses the scale about the root.
                ratio = std::pow(ratio, clampd(p.scaleSpread, 0.25, 2.0));
                return rootF * ratio * std::pow(2.0, (double) octave);
            }

            case TuningMode::AttractorScale:
            {
                // Root frequency preserved; pitch emerges from the attractor.
                const double rootF = 440.0 * std::pow(2.0, (p.rootNote - 69) / 12.0);
                const int    rel   = note - p.rootNote;
                const int    octave = floorDiv(rel, kDegrees);
                return rootF * std::pow(2.0, (double) octave);
            }
        }
        return f_ref;
    }

    // Parameter-state perturbation for Attractor Scale mode (identity otherwise).
    static AttractorState stateForNote(const MatterParams& p, int note)
    {
        AttractorState st;
        if (p.tuningMode != TuningMode::AttractorScale)
            return st;

        const int degree = wrap(note - p.rootNote);
        const double u    = (double) degree / (double) (kDegrees - 1); // 0..1 across the scale
        const double spread = clampd(p.scaleSpread, 0.0, 2.0);

        if (p.attractorSet % 2 == 0)
        {
            // Bank 0: increasing hardening + coupling up the scale -> pitch climbs
            // via amplitude-stiffening; stabilised by stabilityBias.
            st.stiffnessMul   = std::pow(2.0, u * spread);            // ~octave sweep
            st.nonlinStiffMul = 1.0 + 1.5 * u * spread;
            st.couplingMul    = 1.0 + 1.0 * u;
            st.snapBarrierMul = 1.0;
            st.modeRatio      = 1.5 + 0.5 * u;
            st.energyMul      = 1.0 + 0.5 * u;
        }
        else
        {
            // Bank 1: inharmonic mode-ratio ladder — unfamiliar attractor pitches.
            st.stiffnessMul   = unfamiliarRatio(degree) / unfamiliarRatio(0);
            st.nonlinStiffMul = 1.0 + 0.8 * u * spread;
            st.couplingMul    = 1.0 + 0.6 * u;
            st.snapBarrierMul = 1.0 + 0.5 * u;
            st.modeRatio      = unfamiliarRatio((degree + 2) % kDegrees);
            st.energyMul      = 1.0;
        }
        // stabilityBias pulls the perturbations back toward a clean pitched tone.
        const double sb = clampd(p.stabilityBias, 0.0, 1.0);
        st.nonlinStiffMul = lerp(st.nonlinStiffMul, 1.0, sb * 0.5);
        st.couplingMul    = lerp(st.couplingMul,    1.0, sb * 0.5);
        return st;
    }

private:
    static int    wrap(int d)            { int m = d % kDegrees; return m < 0 ? m + kDegrees : m; }
    static int    floorDiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }
    static double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
    static double lerp(double a, double b, double t)     { return a + (b - a) * t; }
};
} // namespace matter
