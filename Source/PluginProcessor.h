// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  MatterTone II — PluginProcessor.h
//  Thin JUCE wrapper around the pure DSP core (matter::MatterSynth). Owns the
//  AudioProcessorValueTreeState, converts it to a matter::MatterParams each
//  block, routes MIDI to the synth, and exposes the synth to the editor for
//  metering. All real-time-unsafe work is kept out of processBlock.
//  v2: LFO A/B modulation (bounded offsets applied BEFORE the Params struct),
//  tempo-synced time overrides, seed policy, percussion engine parameters.
// =============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/MatterSynth.h"
#include "dsp/Presets.h"
#include "common/acv2/dsp/Lfo.h"
#include "common/acv2/dsp/TempoSync.h"

#if MATTER_USE_FOLEYS
 #include <foleys_gui_magic/foleys_gui_magic.h>
 using MatterProcessorBase = foleys::MagicProcessor;   // WYSIWYG editor + state
#else
 using MatterProcessorBase = juce::AudioProcessor;
#endif

namespace pid
{
    // Global
    inline constexpr auto output        = "output";
    inline constexpr auto numVoices     = "numVoices";
    inline constexpr auto oversampling  = "oversampling";
    inline constexpr auto seed          = "seed";
    inline constexpr auto tuningMode    = "tuningMode";
    inline constexpr auto rootNote      = "rootNote";
    inline constexpr auto bodyMix       = "bodyMix";
    inline constexpr auto eventMix      = "eventMix";
    // Material
    inline constexpr auto tension        = "tension";
    inline constexpr auto nonlinStiff    = "nonlinStiffness";
    inline constexpr auto hardening      = "hardening";
    inline constexpr auto damping        = "damping";
    inline constexpr auto nonlinDamping  = "nonlinDamping";
    inline constexpr auto coupling       = "coupling";
    inline constexpr auto brittleness    = "brittleness";
    inline constexpr auto adhesion       = "adhesion";
    inline constexpr auto roughness      = "roughness";
    inline constexpr auto healing        = "healing";
    inline constexpr auto memory         = "memory";
    inline constexpr auto contactHard    = "contactHardness";
    inline constexpr auto contactLoss    = "contactLoss";
    inline constexpr auto crackleCoup    = "crackleCoupling";
    // Excitation
    inline constexpr auto excitation     = "excitation";
    inline constexpr auto attack         = "attack";
    inline constexpr auto pluckEnergy    = "pluckEnergy";
    inline constexpr auto loadingRate    = "loadingRate";
    inline constexpr auto snapDepth      = "snapDepth";
    inline constexpr auto strikePos      = "strikePosition";
    inline constexpr auto velocitySens   = "velocitySens";
    inline constexpr auto agitation      = "agitation";
    // Tone / Scale
    inline constexpr auto modeRatio      = "modeRatio";
    inline constexpr auto attractorSet   = "attractorSet";
    inline constexpr auto scaleSpread    = "scaleSpread";
    inline constexpr auto energyToPitch  = "energyToPitch";
    inline constexpr auto stabilityBias  = "stabilityBias";

    // ---- v2 (registered with versionHint = 2) ----
    // Engine / percussion
    inline constexpr auto engine         = "engine";
    inline constexpr auto membraneModes  = "membraneModes";
    inline constexpr auto bergerAmount   = "bergerAmount";
    inline constexpr auto cavityAmount   = "cavityAmount";
    inline constexpr auto cavityTune     = "cavityTune";
    inline constexpr auto cavityLoss     = "cavityLoss";
    inline constexpr auto wireAmount     = "wireAmount";
    inline constexpr auto dampTilt       = "dampTilt";
    inline constexpr auto malletHard     = "malletHardness";
    inline constexpr auto agc            = "agc";
    inline constexpr auto seedMode       = "seedMode";
    // Time overrides (frozen three-param sync convention + an enable)
    inline constexpr auto attackOvr      = "attackOvr";
    inline constexpr auto attackSync     = "attackSync";
    inline constexpr auto attackMs       = "attackMs";
    inline constexpr auto attackDiv      = "attackDiv";
    inline constexpr auto loadOvr        = "loadOvr";
    inline constexpr auto loadSync       = "loadSync";
    inline constexpr auto loadMs         = "loadMs";
    inline constexpr auto loadDiv        = "loadDiv";
    // LFO A / B
    inline constexpr auto lfoAShape      = "lfoAShape";
    inline constexpr auto lfoASync       = "lfoASync";
    inline constexpr auto lfoARateHz     = "lfoARateHz";
    inline constexpr auto lfoADiv        = "lfoADiv";
    inline constexpr auto lfoAPhase      = "lfoAPhase";
    inline constexpr auto lfoARetrig     = "lfoARetrig";
    inline constexpr auto lfoABipolar    = "lfoABipolar";
    inline constexpr auto lfoBShape      = "lfoBShape";
    inline constexpr auto lfoBSync       = "lfoBSync";
    inline constexpr auto lfoBRateHz     = "lfoBRateHz";
    inline constexpr auto lfoBDiv        = "lfoBDiv";
    inline constexpr auto lfoBPhase      = "lfoBPhase";
    inline constexpr auto lfoBRetrig     = "lfoBRetrig";
    inline constexpr auto lfoBBipolar    = "lfoBBipolar";
    // Mod depths (bounded offsets applied before the Params struct; core unaware)
    inline constexpr auto driveModA      = "driveModA";
    inline constexpr auto driveModB      = "driveModB";
    inline constexpr auto dampModA       = "dampModA";
    inline constexpr auto dampModB       = "dampModB";
    inline constexpr auto coupModA       = "coupModA";
    inline constexpr auto coupModB       = "coupModB";
    inline constexpr auto crackleModA    = "crackleModA";
    inline constexpr auto crackleModB    = "crackleModB";
}

class MatterToneProcessor : public MatterProcessorBase,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::AsyncUpdater
{
public:
    MatterToneProcessor();
    ~MatterToneProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

   #if MATTER_USE_FOLEYS
    // MagicProcessor provides createEditor()/hasEditor()/get|setStateInformation().
    // We only add our own components/LookAndFeels to its builder.
    void initialiseBuilder(foleys::MagicGUIBuilder& builder) override;
   #else
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
   #endif

    const juce::String getName() const override { return "MatterTone II"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return (int) matter::factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

   #if !MATTER_USE_FOLEYS
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
   #endif

    // ---- user presets (message thread only) -----------------------------------
    // Parameter snapshots saved as XML in ~/Music/MatterTone II/Presets
    // (*.mtpreset). Factory programs above stay host-side and read-only.
    static juce::File getUserPresetsDirectory();
    juce::Array<juce::File> getUserPresetFiles() const;
    bool saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::File& presetFile);

    // ---- editor helpers -------------------------------------------------------
    juce::AudioProcessorValueTreeState& state() { return apvts; }
    void panic()              { synth.panic(); }
    void resetMaterialState() { synth.resetMaterialState(); }

    // Copy the rolling output for the scope / scalogram (UI thread).
    void copyScope(std::vector<float>& dst) const { synth.copyScope(dst); }
    double measuredFrequency() const { return synth.measuredFrequency(); }
    int    activeVoices()      const { return synth.activeVoices(); }
    double materialState()     const { return synth.materialStateValue(); }
    int    resetCount()        const { return synth.totalResets(); }
    double currentSampleRate() const { return sr; }
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

    // v2: current LFO outputs (UI mod-ring animation) + last note seed (display).
    float lfoValue(int which) const { return which == 0 ? lfoAValue.load() : lfoBValue.load(); }
    std::uint64_t lastNoteSeed() const { return synth.lastUsedSeed(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    matter::MatterParams paramsFromState() const;
    void applyPreset(const matter::MatterParams& p);
    void setParamRaw(const char* id, float rawValue);
    void configureLfos();

    // Mode-dependent control visibility (foleys binds item visibility to these
    // magicState properties; params all stay registered — UI-only).
    void parameterChanged(const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    static constexpr const char* kViewParams[] { pid::engine, pid::attackOvr, pid::attackSync,
                                                 pid::loadOvr, pid::loadSync,
                                                 pid::lfoASync, pid::lfoBSync };

    double sr = 48000.0;
    int    currentProgram = 0;

    juce::AudioProcessorValueTreeState apvts;
    matter::MatterSynth synth;
    juce::MidiKeyboardState keyboardState;   // drives the editor's on-screen keyboard

    // v2 modulation / tempo
    acv2::Lfo lfoA, lfoB;
    double lastKnownBpm = 120.0;
    int    sigNum = 4, sigDen = 4;
    std::atomic<float> lfoAValue { 0.0f }, lfoBValue { 0.0f };

   #if MATTER_USE_FOLEYS
    foleys::MagicLevelSource*  outputLevel  = nullptr;  // owned by magicState
    foleys::MagicOscilloscope* oscilloscope = nullptr;
    foleys::MagicAnalyser*     analyser     = nullptr;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatterToneProcessor)
};
