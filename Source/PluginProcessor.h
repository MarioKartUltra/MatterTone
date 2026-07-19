// =============================================================================
//  MatterTone POC — PluginProcessor.h
//  Thin JUCE wrapper around the pure DSP core (matter::MatterSynth). Owns the
//  AudioProcessorValueTreeState, converts it to a matter::MatterParams each
//  block, routes MIDI to the synth, and exposes the synth to the editor for
//  metering. All real-time-unsafe work is kept out of processBlock.
// =============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/MatterSynth.h"
#include "dsp/Presets.h"

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
}

class MatterToneProcessor : public MatterProcessorBase
{
public:
    MatterToneProcessor();
    ~MatterToneProcessor() override = default;

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

    const juce::String getName() const override { return "MatterTone POC"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 10; }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

   #if !MATTER_USE_FOLEYS
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
   #endif

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

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    matter::MatterParams paramsFromState() const;
    void applyPreset(const matter::MatterParams& p);
    void setParamRaw(const char* id, float rawValue);

    double sr = 48000.0;
    int    currentProgram = 0;

    juce::AudioProcessorValueTreeState apvts;
    matter::MatterSynth synth;
    juce::MidiKeyboardState keyboardState;   // drives the editor's on-screen keyboard

   #if MATTER_USE_FOLEYS
    foleys::MagicLevelSource*  outputLevel  = nullptr;  // owned by magicState
    foleys::MagicOscilloscope* oscilloscope = nullptr;
    foleys::MagicAnalyser*     analyser     = nullptr;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatterToneProcessor)
};
