// =============================================================================
//  MatterTone POC — PluginProcessor.cpp
// =============================================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
using namespace matter;

namespace
{
    juce::NormalisableRange<float> unitRange() { return { 0.0f, 1.0f, 0.0001f }; }
}

// -----------------------------------------------------------------------------
APVTS::ParameterLayout MatterToneProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto fParam = [&](const char* id, const char* name, juce::NormalisableRange<float> r, float def)
    {
        p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, r, def));
    };
    auto uParam = [&](const char* id, const char* name, float def) { fParam(id, name, unitRange(), def); };
    auto cParam = [&](const char* id, const char* name, juce::StringArray choices, int def)
    {
        p.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, name, choices, def));
    };
    auto iParam = [&](const char* id, const char* name, int lo, int hi, int def)
    {
        p.push_back(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { id, 1 }, name, lo, hi, def));
    };

    // ---- Global ----
    fParam(pid::output, "Output", { 0.0f, 2.0f, 0.001f }, 0.7f);
    iParam(pid::numVoices, "Polyphony", 1, 8, 8);
    cParam(pid::oversampling, "Oversampling", { "2x", "4x", "8x" }, 1);
    iParam(pid::seed, "Seed", 0, 9999, 1);
    cParam(pid::tuningMode, "Tuning Mode", { "Chromatic", "Ratio Set", "Attractor Scale" }, 0);
    iParam(pid::rootNote, "Root Note", 24, 84, 60);
    uParam(pid::bodyMix, "Body Mix", 0.25f);
    uParam(pid::eventMix, "Event Mix", 0.5f);

    // ---- Material ----
    fParam(pid::tension, "Tension", { 0.25f, 4.0f, 0.0f, 0.5f }, 1.0f);
    uParam(pid::nonlinStiff,   "Nonlinear Stiffness", 0.35f);
    fParam(pid::hardening, "Hardening / Softening", { -1.0f, 1.0f, 0.001f }, 0.4f);
    uParam(pid::damping,       "Damping", 0.15f);
    uParam(pid::nonlinDamping, "Nonlinear Damping", 0.08f);
    uParam(pid::coupling,      "Coupling", 0.25f);
    uParam(pid::brittleness,   "Brittleness", 0.4f);
    uParam(pid::adhesion,      "Adhesion", 0.3f);
    uParam(pid::roughness,     "Roughness / Heterogeneity", 0.5f);
    uParam(pid::healing,       "Healing", 0.3f);
    uParam(pid::memory,        "Memory", 0.4f);
    uParam(pid::contactHard,   "Contact Hardness", 0.3f);
    uParam(pid::contactLoss,   "Contact Loss", 0.3f);
    uParam(pid::crackleCoup,   "Crackle Coupling", 0.3f);

    // ---- Excitation ----
    cParam(pid::excitation, "Excitation Mode", { "Pluck", "Load / Snap" }, 0);
    uParam(pid::attack,       "Attack", 0.1f);
    uParam(pid::pluckEnergy,  "Pluck Energy", 0.5f);
    uParam(pid::loadingRate,  "Loading Rate", 0.4f);
    uParam(pid::snapDepth,    "Snap Depth", 0.5f);
    uParam(pid::strikePos,    "Strike Position", 0.3f);
    uParam(pid::velocitySens, "Velocity Sensitivity", 0.7f);
    uParam(pid::agitation,    "Agitation", 0.0f);

    // ---- Tone / Scale ----
    fParam(pid::modeRatio, "Mode Ratio", { 0.5f, 4.0f, 0.0f, 0.5f }, 1.5f);
    iParam(pid::attractorSet, "Attractor Set", 0, 3, 0);
    fParam(pid::scaleSpread, "Scale Spread", { 0.25f, 2.0f, 0.001f }, 1.0f);
    uParam(pid::energyToPitch, "Energy to Pitch", 0.3f);
    uParam(pid::stabilityBias, "Stability Bias", 0.5f);

    return { p.begin(), p.end() };
}

// -----------------------------------------------------------------------------
MatterToneProcessor::MatterToneProcessor()
    : MatterProcessorBase(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MATTERTONE", createLayout())
{
   #if MATTER_USE_FOLEYS
    // Let the WYSIWYG editor auto-save GUI edits back next to the source (design collab).
    FOLEYS_SET_SOURCE_PATH(__FILE__);
    // Register real-time visualisers the GUI can display (fed in processBlock).
    outputLevel  = magicState.createAndAddObject<foleys::MagicLevelSource>("output");
    oscilloscope = magicState.createAndAddObject<foleys::MagicOscilloscope>("waveform");
    analyser     = magicState.createAndAddObject<foleys::MagicAnalyser>("analyser");
   #endif
}

#if MATTER_USE_FOLEYS
void MatterToneProcessor::initialiseBuilder(foleys::MagicGUIBuilder& builder)
{
    builder.registerJUCEFactories();
    builder.registerJUCELookAndFeels();
   #if MATTER_USE_FCWT
    // Expose the fCWT scalogram as a draggable widget in the WYSIWYG palette.
    builder.registerFactory("Scalogram", &ScalogramItem::factory);
   #endif
}
#endif

MatterParams MatterToneProcessor::paramsFromState() const
{
    auto raw = [&](const char* id) { return apvts.getRawParameterValue(id)->load(); };

    MatterParams m;
    m.output          = raw(pid::output);
    m.numVoices       = (int) raw(pid::numVoices);
    const int osIdx   = (int) raw(pid::oversampling);
    m.oversampling    = (osIdx == 0 ? 2 : (osIdx == 1 ? 4 : 8));
    m.seed            = (unsigned) raw(pid::seed);
    m.tuningMode      = (TuningMode) (int) raw(pid::tuningMode);
    m.rootNote        = (int) raw(pid::rootNote);
    m.bodyMix         = raw(pid::bodyMix);
    m.eventMix        = raw(pid::eventMix);

    m.tension         = raw(pid::tension);
    m.nonlinStiffness = raw(pid::nonlinStiff);
    m.hardening       = raw(pid::hardening);
    m.damping         = raw(pid::damping);
    m.nonlinDamping   = raw(pid::nonlinDamping);
    m.coupling        = raw(pid::coupling);
    m.brittleness     = raw(pid::brittleness);
    m.adhesion        = raw(pid::adhesion);
    m.roughness       = raw(pid::roughness);
    m.healing         = raw(pid::healing);
    m.memory          = raw(pid::memory);
    m.contactHardness = raw(pid::contactHard);
    m.contactLoss     = raw(pid::contactLoss);
    m.crackleCoupling = raw(pid::crackleCoup);

    m.excitation      = (ExcitationMode) (int) raw(pid::excitation);
    m.attack          = raw(pid::attack);
    m.pluckEnergy     = raw(pid::pluckEnergy);
    m.loadingRate     = raw(pid::loadingRate);
    m.snapDepth       = raw(pid::snapDepth);
    m.strikePosition  = raw(pid::strikePos);
    m.velocitySens    = raw(pid::velocitySens);
    m.agitation       = raw(pid::agitation);

    m.modeRatio       = raw(pid::modeRatio);
    m.attractorSet    = (int) raw(pid::attractorSet);
    m.scaleSpread     = raw(pid::scaleSpread);
    m.energyToPitch   = raw(pid::energyToPitch);
    m.stabilityBias   = raw(pid::stabilityBias);
    return m;
}

// -----------------------------------------------------------------------------
void MatterToneProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synth.prepare(sampleRate, samplesPerBlock);
    synth.setParameters(paramsFromState());

   #if MATTER_USE_FOLEYS
    magicState.prepareToPlay(sampleRate, samplesPerBlock);
    if (outputLevel != nullptr)
        outputLevel->setupSource(getTotalNumOutputChannels(), sampleRate, 500);
   #endif
}

bool MatterToneProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void MatterToneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Push current parameter state to the synth (also handles oversampling change).
    synth.setParameters(paramsFromState());

    // Inject notes played on the editor's on-screen keyboard into the MIDI stream.
    keyboardState.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);

    // Route MIDI. Block-granular timing is sufficient for the POC.
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())            synth.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())      synth.noteOff(msg.getNoteNumber());
        else if (msg.isPitchWheel())   synth.pitchBend((msg.getPitchWheelValue() - 8192) / 8192.0 * 2.0);
        else if (msg.isController() && msg.getControllerNumber() == 1)
                                       synth.modWheelChanged(msg.getControllerValue() / 127.0);
        else if (msg.isChannelPressure())
                                       synth.aftertouchChanged(msg.getChannelPressureValue() / 127.0);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                                       synth.panic();
    }

    buffer.clear();
    const int n = buffer.getNumSamples();
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    synth.renderBlock(L, R, n);

   #if MATTER_USE_FOLEYS
    if (outputLevel  != nullptr) outputLevel->pushSamples(buffer);
    if (oscilloscope != nullptr) oscilloscope->pushSamples(buffer);
    if (analyser     != nullptr) analyser->pushSamples(buffer);
   #endif
}

// -----------------------------------------------------------------------------
#if !MATTER_USE_FOLEYS
juce::AudioProcessorEditor* MatterToneProcessor::createEditor()
{
    return new MatterToneEditor(*this);
}
#endif

// ---- programs / presets ------------------------------------------------------
const juce::String MatterToneProcessor::getProgramName(int index)
{
    auto presets = factoryPresets();
    if (index >= 0 && index < (int) presets.size()) return presets[(size_t) index].name;
    return {};
}

void MatterToneProcessor::setParamRaw(const char* id, float rawValue)
{
    if (auto* param = apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(rawValue));
}

void MatterToneProcessor::applyPreset(const MatterParams& m)
{
    setParamRaw(pid::output, (float) m.output);
    setParamRaw(pid::numVoices, (float) m.numVoices);
    setParamRaw(pid::oversampling, (float) (m.oversampling == 2 ? 0 : (m.oversampling == 4 ? 1 : 2)));
    setParamRaw(pid::seed, (float) m.seed);
    setParamRaw(pid::tuningMode, (float) (int) m.tuningMode);
    setParamRaw(pid::rootNote, (float) m.rootNote);
    setParamRaw(pid::bodyMix, (float) m.bodyMix);
    setParamRaw(pid::eventMix, (float) m.eventMix);

    setParamRaw(pid::tension, (float) m.tension);
    setParamRaw(pid::nonlinStiff, (float) m.nonlinStiffness);
    setParamRaw(pid::hardening, (float) m.hardening);
    setParamRaw(pid::damping, (float) m.damping);
    setParamRaw(pid::nonlinDamping, (float) m.nonlinDamping);
    setParamRaw(pid::coupling, (float) m.coupling);
    setParamRaw(pid::brittleness, (float) m.brittleness);
    setParamRaw(pid::adhesion, (float) m.adhesion);
    setParamRaw(pid::roughness, (float) m.roughness);
    setParamRaw(pid::healing, (float) m.healing);
    setParamRaw(pid::memory, (float) m.memory);
    setParamRaw(pid::contactHard, (float) m.contactHardness);
    setParamRaw(pid::contactLoss, (float) m.contactLoss);
    setParamRaw(pid::crackleCoup, (float) m.crackleCoupling);

    setParamRaw(pid::excitation, (float) (int) m.excitation);
    setParamRaw(pid::attack, (float) m.attack);
    setParamRaw(pid::pluckEnergy, (float) m.pluckEnergy);
    setParamRaw(pid::loadingRate, (float) m.loadingRate);
    setParamRaw(pid::snapDepth, (float) m.snapDepth);
    setParamRaw(pid::strikePos, (float) m.strikePosition);
    setParamRaw(pid::velocitySens, (float) m.velocitySens);
    setParamRaw(pid::agitation, (float) m.agitation);

    setParamRaw(pid::modeRatio, (float) m.modeRatio);
    setParamRaw(pid::attractorSet, (float) m.attractorSet);
    setParamRaw(pid::scaleSpread, (float) m.scaleSpread);
    setParamRaw(pid::energyToPitch, (float) m.energyToPitch);
    setParamRaw(pid::stabilityBias, (float) m.stabilityBias);
}

void MatterToneProcessor::setCurrentProgram(int index)
{
    auto presets = factoryPresets();
    if (index < 0 || index >= (int) presets.size()) return;
    currentProgram = index;
    applyPreset(presets[(size_t) index].params);
}

// ---- state -------------------------------------------------------------------
#if !MATTER_USE_FOLEYS
void MatterToneProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto stateTree = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(stateTree.createXml());
    xml->setAttribute("program", currentProgram);
    copyXmlToBinary(*xml, destData);
}

void MatterToneProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType())) return;
    currentProgram = xml->getIntAttribute("program", 0);
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
}
#endif // !MATTER_USE_FOLEYS

// -----------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MatterToneProcessor();
}
