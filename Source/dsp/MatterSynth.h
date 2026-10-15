// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  MatterTone POC — MatterSynth.h
//  Polyphony manager over MatterVoice: MIDI note allocation + stealing, live
//  parameter propagation, stereo mixdown, output gain / DC-safe summing, and a
//  lock-free-ish rolling output buffer for UI pitch/scalogram display.
//  Pure C++17, NO JUCE dependency.
// =============================================================================
#pragma once
#include <cmath>

#include <array>
#include <vector>
#include <cstdint>
#include "Config.h"
#include "MatterParams.h"
#include "MatterVoice.h"
#include "PitchDetector.h"
#include "../common/acv2/dsp/SeedPolicy.h"

namespace matter
{
class MatterSynth
{
public:
    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sr = sampleRate;
        for (auto& v : voices) { v.prepare(sampleRate); }
        for (auto& a : age) a = 0;
        scopeBuf.assign(scopeSize, 0.0f);
        scopePos = 0;
        applyOversampling(params.oversampling);
        // the output gain glides to its target over kGainGlideSecs: a knob moved
        // while a note sounds must not step the waveform at every block boundary
        gainCoeff = 1.0 - std::exp(-1.0 / (kGainGlideSecs * std::max(1.0, sr)));
        gainLive  = params.output;
    }

    void setParameters(const MatterParams& p)
    {
        const int prevOS = params.oversampling;
        params = p;
        if (p.oversampling != prevOS) applyOversampling(p.oversampling);
        // v2 seed policy follows the parameter state (cheap, allocation-free).
        seedPolicy.setMode((acv2::SeedMode) (int) params.seedMode);
        if (params.seed != lastGlobalSeed)
        {
            lastGlobalSeed = params.seed;
            seedPolicy.setGlobalSeed((std::uint64_t) params.seed);
        }
        // propagate live edits to sounding voices
        for (auto& v : voices) if (v.isActive()) v.updateParameters(params);
    }

    const MatterParams& parameters() const { return params; }

    // ---- MIDI -----------------------------------------------------------------
    void noteOn(int midiNote, double velocity01)
    {
        const int maxV = std::min(std::max(params.numVoices, 1), cfg::kMaxVoices);
        int slot = -1;

        // free voice within the allowed polyphony?
        for (int i = 0; i < maxV; ++i)
            if (!voices[i].isActive()) { slot = i; break; }

        // else steal the quietest (lowest output envelope), preferring released ones.
        if (slot < 0)
        {
            double bestScore = 1.0e300;
            for (int i = 0; i < maxV; ++i)
            {
                const double relBonus = voices[i].isReleased() ? 0.0 : 1.0e6;
                const double score = voices[i].lastOutputPeak() + relBonus;
                if (score < bestScore) { bestScore = score; slot = i; }
            }
        }
        if (slot < 0) slot = 0;

        voices[slot].setPitchBendSemitones(pitchBendSemis);
        voices[slot].setModWheel(modWheel);
        voices[slot].setAftertouch(aftertouch);
        voices[slot].setInitialMemory(materialState);   // persistent history
        params.noteSeed = seedPolicy.onNoteOn(midiNote); // v2: policy-driven seed
        voices[slot].noteOn(midiNote, velocity01, params);
        lastVoice = slot;
        age[slot] = ++ageCounter;
    }

    // v2: last seed handed to a voice (UI display / "lock this seed").
    std::uint64_t lastUsedSeed() const { return seedPolicy.lastUsedSeed(); }

    // Reset the persistent material state and heal any sounding voices.
    void resetMaterialState()
    {
        materialState = 1.0;
        for (auto& v : voices) v.healMemory();
    }
    double materialStateValue() const { return materialState; }

    void noteOff(int midiNote)
    {
        for (int i = 0; i < cfg::kMaxVoices; ++i)
            if (voices[i].isActive() && !voices[i].isReleased()
                && voices[i].midiNote() == midiNote)
                voices[i].noteOff();
    }

    void pitchBend(double semis)   { pitchBendSemis = semis; for (auto& v : voices) v.setPitchBendSemitones(semis); }
    void modWheelChanged(double v) { modWheel = v;           for (auto& x : voices) x.setModWheel(v); }
    void aftertouchChanged(double v){ aftertouch = v;        for (auto& x : voices) x.setAftertouch(v); }

    void panic() { for (auto& v : voices) v.panic(); }

    // ---- audio ---------------------------------------------------------------
    // Renders one block of mono synthesis, duplicated to stereo out (L==R).
    void renderBlock(float* left, float* right, int numSamples)
    {
        const double gain = params.output;
        // Per-SAMPLE material-memory time constant (block-size independent, so
        // renders are bit-identical regardless of host buffer size).
        constexpr double materialCoeff = 1.0e-4;

        for (int n = 0; n < numSamples; ++n)
        {
            double mix = 0.0;
            for (int i = 0; i < cfg::kMaxVoices; ++i)
                if (voices[i].isActive())
                    mix += voices[i].renderSample();

            gainLive += (gain - gainLive) * gainCoeff;   // per sample, so the step is inaudible
            mix *= gainLive;
            if (!std::isfinite(mix)) mix = 0.0;          // never emit NaN/Inf
            if (mix >  1.5) mix =  1.5;                  // final gentle safety clamp
            if (mix < -1.5) mix = -1.5;

            const float f = (float) mix;
            if (left)  left[n]  = f;
            if (right) right[n] = f;

            scopeBuf[scopePos] = f;
            scopePos = (scopePos + 1) % scopeSize;

            // Fold the most-recently-triggered voice's memory into the shared,
            // persistent material state (history-dependent, deterministic).
            if (lastVoice >= 0 && voices[lastVoice].isActive())
                materialState += materialCoeff * (voices[lastVoice].memoryValue() - materialState);
        }
    }

    // ---- diagnostics (UI thread) ---------------------------------------------
    int activeVoices() const
    {
        int c = 0;
        for (auto& v : voices) if (v.isActive()) ++c;
        return c;
    }

    double totalStoredEnergy() const
    {
        double e = 0.0;
        for (auto& v : voices) if (v.isActive()) e += v.storedEnergy();
        return e;
    }

    int totalResets() const
    {
        int r = 0;
        for (auto& v : voices) r += v.resets();
        return r;
    }

    // Copy the rolling output history (chronological) for UI pitch / scalogram.
    void copyScope(std::vector<float>& dst) const
    {
        dst.resize(scopeSize);
        for (int i = 0; i < scopeSize; ++i)
            dst[i] = scopeBuf[(scopePos + i) % scopeSize];
    }

    // Measured dominant frequency after onset (UI/offline; not audio-thread cheap).
    double measuredFrequency() const
    {
        std::vector<float> tmp;
        copyScope(tmp);
        return PitchDetector::estimate(tmp, sr);
    }

    MatterVoice&       voice(int i)       { return voices[i]; }
    const MatterVoice& voice(int i) const { return voices[i]; }

private:
    void applyOversampling(int factor)
    {
        for (auto& v : voices) v.setOversampling(factor);
    }

    double sr = 48000.0;

    static constexpr double kGainGlideSecs = 0.010;

    double gainLive = 0.7, gainCoeff = 1.0;   // the output gain in force, and its per-sample approach
    MatterParams params;
    std::array<MatterVoice, cfg::kMaxVoices> voices;
    std::array<std::uint64_t, cfg::kMaxVoices> age {};
    std::uint64_t ageCounter = 0;

    double pitchBendSemis = 0.0;
    double modWheel = 0.0;
    double aftertouch = 0.0;
    double materialState = 1.0;   // persistent, history-dependent material memory
    int    lastVoice = -1;

    acv2::SeedPolicy seedPolicy;
    unsigned lastGlobalSeed = ~0u; // force the first setGlobalSeed

    static constexpr int scopeSize = 8192;
    std::vector<float> scopeBuf;
    int scopePos = 0;
};
} // namespace matter
