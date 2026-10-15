// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  MatterTone II — PluginProcessor.cpp
// =============================================================================
#include "PluginProcessor.h"
#include "PluginEditor.h"

#if MATTER_USE_FOLEYS
 #include "BinaryData.h"
 #include "common/acv2/ui/Theme.h"
 #include "common/acv2/ui/LnfV2.h"
#endif

using APVTS = juce::AudioProcessorValueTreeState;
using namespace matter;

namespace
{
    juce::NormalisableRange<float> unitRange() { return { 0.0f, 1.0f, 0.0001f }; }

    juce::StringArray divisionLabels()
    {
        juce::StringArray out;
        for (int i = 0; i < acv2::tempo::kNumDivisions; ++i)
            out.add(acv2::tempo::kDivisions[i].label);
        return out;
    }

    juce::AudioParameterFloatAttributes msAttributes()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return v < 100.0f ? juce::String(v, 1) + " ms"
                                                 : juce::String(juce::roundToInt(v)) + " ms"; });
    }

    // "35 %" out, and "35 %" or "35" back in (without the second half, typing a
    // percentage into a knob's text box read as 35 = far above 1 = full scale)
    juce::AudioParameterFloatAttributes pctAttributes()
    {
        return juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction([](float v, int)
                { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; })
            .withValueFromStringFunction([](const juce::String& t)
                { return t.retainCharacters("+-0123456789.,").replaceCharacter(',', '.').getFloatValue() / 100.0f; });
    }

    // two decimals, and no "-0.00"
    juce::AudioParameterFloatAttributes plainAttributes()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int)
            {
                const float r = std::round(v * 100.0f) / 100.0f;
                return juce::String(std::abs(r) < 0.005f ? 0.0f : r, 2);
            });
    }

    juce::AudioParameterFloatAttributes hzAttributes()
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, v < 10.0f ? 2 : 1) + " Hz"; });
    }
}

// -----------------------------------------------------------------------------
APVTS::ParameterLayout MatterToneProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // the readout functions change what a knob says, not what is stored
    auto fParam = [&](const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                      juce::AudioParameterFloatAttributes attr = plainAttributes())
    {
        p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, r, def, attr));
    };
    auto uParam = [&](const char* id, const char* name, float def) { fParam(id, name, unitRange(), def, pctAttributes()); };
    auto cParam = [&](const char* id, const char* name, juce::StringArray choices, int def)
    {
        p.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 1 }, name, choices, def));
    };
    auto iParam = [&](const char* id, const char* name, int lo, int hi, int def)
    {
        p.push_back(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { id, 1 }, name, lo, hi, def));
    };

    // ---- v2 helpers (versionHint = 2) ----
    auto fParam2 = [&](const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                       juce::AudioParameterFloatAttributes attr = plainAttributes())
    {
        p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 2 }, name, r, def, attr));
    };
    auto uParam2 = [&](const char* id, const char* name, float def) { fParam2(id, name, unitRange(), def, pctAttributes()); };
    auto cParam2 = [&](const char* id, const char* name, juce::StringArray choices, int def)
    {
        p.push_back(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { id, 2 }, name, choices, def));
    };
    auto iParam2 = [&](const char* id, const char* name, int lo, int hi, int def)
    {
        p.push_back(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { id, 2 }, name, lo, hi, def));
    };
    auto bParam2 = [&](const char* id, const char* name, bool def)
    {
        p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 2 }, name, def));
    };
    auto depth2 = [&](const char* id, const char* name)
    {
        fParam2(id, name, { -1.0f, 1.0f, 0.001f }, 0.0f, pctAttributes());
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

    // =========================================================================
    //  v2 parameters
    // =========================================================================
    // ---- Engine / percussion ----
    cParam2(pid::engine, "Engine", { "Modal", "Percussion" }, 0);
    iParam2(pid::membraneModes, "Membrane Modes", 2, matter::cfg::kMaxMembraneModes, 6);
    uParam2(pid::bergerAmount, "Berger Tension", 0.5f);
    uParam2(pid::cavityAmount, "Cavity Amount", 0.4f);
    fParam2(pid::cavityTune, "Cavity Tune", { 0.1f, 1.5f, 0.001f }, 0.45f);
    uParam2(pid::cavityLoss, "Cavity Loss", 0.4f);
    uParam2(pid::wireAmount, "Snare Wires", 0.0f);
    uParam2(pid::dampTilt, "Damp Tilt", 0.3f);
    uParam2(pid::malletHard, "Mallet Hardness", 0.6f);
    uParam2(pid::agc, "Attack AGC", 1.0f);
    cParam2(pid::seedMode, "Seed Mode", { "Fixed", "Per-Key", "Random on Strike" }, 1);

    // ---- Time overrides (ms / beats trios) ----
    const auto divs = divisionLabels();
    bParam2(pid::attackOvr, "Attack Time On", false);
    bParam2(pid::attackSync, "Attack Time Sync", false);
    fParam2(pid::attackMs, "Attack Time", { 0.3f, 1000.0f, 0.0f, 0.35f }, 5.0f, msAttributes());
    cParam2(pid::attackDiv, "Attack Beats", divs, acv2::tempo::kDefaultDivision);
    bParam2(pid::loadOvr, "Load Time On", false);
    bParam2(pid::loadSync, "Load Time Sync", false);
    fParam2(pid::loadMs, "Load Time", { 20.0f, 10000.0f, 0.0f, 0.35f }, 400.0f, msAttributes());
    cParam2(pid::loadDiv, "Load Beats", divs, acv2::tempo::kDefaultDivision);

    // ---- LFO A / B ----
    const juce::StringArray shapes { "Sine", "Triangle", "Saw Up", "Saw Down",
                                     "Square", "Sample & Hold", "Smooth Random", "Drift" };
    const juce::StringArray retrigs { "Free", "Key", "Transport" };
    auto lfoParams = [&](const char* shape, const char* sync, const char* rate, const char* div,
                         const char* phase, const char* retrig, const char* bipolar, const char* tag)
    {
        cParam2(shape, (juce::String("LFO ") + tag + " Shape").toRawUTF8(), shapes, 0);
        bParam2(sync,  (juce::String("LFO ") + tag + " Sync").toRawUTF8(), false);
        fParam2(rate,  (juce::String("LFO ") + tag + " Rate").toRawUTF8(),
                { 0.01f, 40.0f, 0.0f, 0.35f }, 1.0f, hzAttributes());
        cParam2(div,   (juce::String("LFO ") + tag + " Beats").toRawUTF8(), divs, acv2::tempo::kDefaultDivision);
        fParam2(phase, (juce::String("LFO ") + tag + " Phase").toRawUTF8(), { 0.0f, 1.0f, 0.001f }, 0.0f, pctAttributes());
        cParam2(retrig,(juce::String("LFO ") + tag + " Retrig").toRawUTF8(), retrigs, 0);
        bParam2(bipolar,(juce::String("LFO ") + tag + " Bipolar").toRawUTF8(), true);
    };
    lfoParams(pid::lfoAShape, pid::lfoASync, pid::lfoARateHz, pid::lfoADiv,
              pid::lfoAPhase, pid::lfoARetrig, pid::lfoABipolar, "A");
    lfoParams(pid::lfoBShape, pid::lfoBSync, pid::lfoBRateHz, pid::lfoBDiv,
              pid::lfoBPhase, pid::lfoBRetrig, pid::lfoBBipolar, "B");

    // ---- Mod depths ----
    depth2(pid::driveModA,   "Drive Mod A");
    depth2(pid::driveModB,   "Drive Mod B");
    depth2(pid::dampModA,    "Damping Mod A");
    depth2(pid::dampModB,    "Damping Mod B");
    depth2(pid::coupModA,    "Coupling Mod A");
    depth2(pid::coupModB,    "Coupling Mod B");
    depth2(pid::crackleModA, "Crackle Mod A");
    depth2(pid::crackleModB, "Crackle Mod B");

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
    // Designed default layout (Resources/magic.xml baked into BinaryData).
    magicState.setGuiValueTree(BinaryData::magic_xml, BinaryData::magic_xmlSize);
    // Register real-time visualisers the GUI can display (fed in processBlock).
    outputLevel  = magicState.createAndAddObject<foleys::MagicLevelSource>("output");
    oscilloscope = magicState.createAndAddObject<foleys::MagicOscilloscope>("waveform");
    analyser     = magicState.createAndAddObject<foleys::MagicAnalyser>("analyser");
   #endif

    // Mode-dependent visibility: keep every param registered; only the UI reacts.
    for (auto* id : kViewParams)
        apvts.addParameterListener(id, this);
    handleAsyncUpdate();
}

MatterToneProcessor::~MatterToneProcessor()
{
    for (auto* id : kViewParams)
        apvts.removeParameterListener(id, this);
    cancelPendingUpdate();
}

// Which of several alternative controls is on screen. A time can be set three
// ways: the classic 0..1 knob, an absolute time ("timed"), or a beat division
// ("timed" + "beats"); an LFO rate is in Hz or in beats.
void MatterToneProcessor::handleAsyncUpdate()
{
   #if MATTER_USE_FOLEYS
    auto on  = [this](const char* id) { return apvts.getRawParameterValue(id)->load() > 0.5f; };
    auto set = [this](const char* prop, bool v) { magicState.getPropertyAsValue(prop).setValue(v); };

    const bool perc = (int) apvts.getRawParameterValue(pid::engine)->load() == 1;
    set("showModal", ! perc);
    set("showPerc",  perc);

    const bool attackTimed = on(pid::attackOvr), attackBeats = on(pid::attackSync);
    set("view:attackKnob",  ! attackTimed);
    set("view:attackTimed", attackTimed);
    set("view:attackMs",    attackTimed && ! attackBeats);
    set("view:attackDiv",   attackTimed && attackBeats);

    const bool loadTimed = on(pid::loadOvr), loadBeats = on(pid::loadSync);
    set("view:loadKnob",  ! loadTimed);
    set("view:loadTimed", loadTimed);
    set("view:loadMs",    loadTimed && ! loadBeats);
    set("view:loadDiv",   loadTimed && loadBeats);

    set("view:lfoAHz",  ! on(pid::lfoASync));
    set("view:lfoADiv", on(pid::lfoASync));
    set("view:lfoBHz",  ! on(pid::lfoBSync));
    set("view:lfoBDiv", on(pid::lfoBSync));
   #endif
}

#if MATTER_USE_FOLEYS
void MatterToneProcessor::initialiseBuilder(foleys::MagicGUIBuilder& builder)
{
    builder.registerJUCEFactories();
    builder.registerJUCELookAndFeels();
    builder.registerLookAndFeel("LnfV2", std::make_unique<acv2::ui::LnfV2>(acv2::theme::kMatterTone));
    builder.registerFactory("Sigil", &SigilItem::factory);
    builder.registerFactory("PresetMenu", &PresetMenuItem::factory);
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

    // ---- v2 ----
    m.engine          = (EngineMode) (int) raw(pid::engine);
    m.membraneModes   = (int) raw(pid::membraneModes);
    m.bergerAmount    = raw(pid::bergerAmount);
    m.cavityAmount    = raw(pid::cavityAmount);
    m.cavityTune      = raw(pid::cavityTune);
    m.cavityLoss      = raw(pid::cavityLoss);
    m.wireAmount      = raw(pid::wireAmount);
    m.dampTilt        = raw(pid::dampTilt);
    m.malletHardness  = raw(pid::malletHard);
    m.agc             = raw(pid::agc);
    m.seedMode        = (SeedModeV2) (int) raw(pid::seedMode);

    // Time overrides (0 = keep the v1 normalised-knob behaviour).
    if (raw(pid::attackOvr) > 0.5f)
        m.attackMsOverride = raw(pid::attackSync) > 0.5f
            ? acv2::tempo::divisionToMs((int) raw(pid::attackDiv), lastKnownBpm, sigNum, sigDen)
            : (double) raw(pid::attackMs);
    if (raw(pid::loadOvr) > 0.5f)
        m.loadTimeMsOverride = raw(pid::loadSync) > 0.5f
            ? acv2::tempo::divisionToMs((int) raw(pid::loadDiv), lastKnownBpm, sigNum, sigDen)
            : (double) raw(pid::loadMs);
    return m;
}

// -----------------------------------------------------------------------------
void MatterToneProcessor::configureLfos()
{
    auto raw = [&](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    auto cfg = [&](acv2::Lfo& l, const char* shape, const char* sync, const char* rate,
                   const char* div, const char* phase, const char* retrig, const char* bipolar)
    {
        l.setBpm(lastKnownBpm);
        l.setShape((acv2::LfoShape) (int) raw(shape));
        l.setRetrigMode((acv2::LfoRetrig) (int) raw(retrig));
        l.setBipolar(raw(bipolar) > 0.5f);
        l.setPhaseOffset((double) raw(phase));
        if (raw(sync) > 0.5f) l.setSyncedDivision((int) raw(div));
        else                  l.setRateHz((double) raw(rate));
    };
    cfg(lfoA, pid::lfoAShape, pid::lfoASync, pid::lfoARateHz, pid::lfoADiv,
        pid::lfoAPhase, pid::lfoARetrig, pid::lfoABipolar);
    cfg(lfoB, pid::lfoBShape, pid::lfoBSync, pid::lfoBRateHz, pid::lfoBDiv,
        pid::lfoBPhase, pid::lfoBRetrig, pid::lfoBBipolar);
}

void MatterToneProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synth.prepare(sampleRate, samplesPerBlock);
    lfoA.prepare(sampleRate);
    lfoB.prepare(sampleRate);
    configureLfos();
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
    const int n = buffer.getNumSamples();

    // ---- host tempo / transport (all Optional; cache last known) ----
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())            lastKnownBpm = *b;
            if (auto sig = pos->getTimeSignature()) { sigNum = sig->numerator; sigDen = sig->denominator; }
            if (auto ppq = pos->getPpqPosition())   { lfoA.setTransportPpq(*ppq); lfoB.setTransportPpq(*ppq); }
        }

    configureLfos();

    // Inject notes played on the editor's on-screen keyboard into the MIDI stream.
    keyboardState.processNextMidiBuffer(midi, 0, n, true);

    // Route MIDI. Block-granular timing is sufficient here.
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            synth.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
            lfoA.noteOn();  // no-op unless retrig mode == Key
            lfoB.noteOn();
        }
        else if (msg.isNoteOff())      synth.noteOff(msg.getNoteNumber());
        else if (msg.isPitchWheel())   synth.pitchBend((msg.getPitchWheelValue() - 8192) / 8192.0 * 2.0);
        else if (msg.isController() && msg.getControllerNumber() == 1)
                                       synth.modWheelChanged(msg.getControllerValue() / 127.0);
        else if (msg.isChannelPressure())
                                       synth.aftertouchChanged(msg.getChannelPressureValue() / 127.0);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                                       synth.panic();
    }

    // ---- LFO A/B → bounded offsets BEFORE the Params struct (core stays unaware).
    const float a = lfoA.processBlock(n);
    const float b = lfoB.processBlock(n);
    lfoAValue.store(a, std::memory_order_relaxed);
    lfoBValue.store(b, std::memory_order_relaxed);

    auto raw = [&](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    MatterParams m = paramsFromState();
    auto mod = [&](double v, const char* da, const char* db)
    {
        return juce::jlimit(0.0, 1.0, v + (double) (a * raw(da)) + (double) (b * raw(db)));
    };
    m.pluckEnergy     = mod(m.pluckEnergy,     pid::driveModA,   pid::driveModB);
    m.damping         = mod(m.damping,         pid::dampModA,    pid::dampModB);
    m.coupling        = mod(m.coupling,        pid::coupModA,    pid::coupModB);
    m.crackleCoupling = mod(m.crackleCoupling, pid::crackleModA, pid::crackleModB);
    synth.setParameters(m);   // core clamps everything again in computeDesign

    buffer.clear();
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

    // ---- v2 ----
    setParamRaw(pid::engine, (float) (int) m.engine);
    setParamRaw(pid::membraneModes, (float) m.membraneModes);
    setParamRaw(pid::bergerAmount, (float) m.bergerAmount);
    setParamRaw(pid::cavityAmount, (float) m.cavityAmount);
    setParamRaw(pid::cavityTune, (float) m.cavityTune);
    setParamRaw(pid::cavityLoss, (float) m.cavityLoss);
    setParamRaw(pid::wireAmount, (float) m.wireAmount);
    setParamRaw(pid::dampTilt, (float) m.dampTilt);
    setParamRaw(pid::malletHard, (float) m.malletHardness);
    setParamRaw(pid::agc, (float) m.agc);
    setParamRaw(pid::seedMode, (float) (int) m.seedMode);
}

void MatterToneProcessor::setCurrentProgram(int index)
{
    auto presets = factoryPresets();
    if (index < 0 || index >= (int) presets.size()) return;
    currentProgram = index;
    applyPreset(presets[(size_t) index].params);
}

// ---- user presets (~/Music/MatterTone II/Presets) ----------------------------
juce::File MatterToneProcessor::getUserPresetsDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory)
               .getChildFile("MatterTone II")
               .getChildFile("Presets");
}

juce::Array<juce::File> MatterToneProcessor::getUserPresetFiles() const
{
    auto files = getUserPresetsDirectory().findChildFiles(juce::File::findFiles, false, "*.mtpreset");
    files.sort();
    return files;
}

bool MatterToneProcessor::saveUserPreset(const juce::String& name)
{
    const auto safe = juce::File::createLegalFileName(name.trim());
    if (safe.isEmpty()) return false;

    auto dir = getUserPresetsDirectory();
    if (! dir.createDirectory()) return false;

    auto xml = apvts.copyState().createXml();
    if (xml == nullptr) return false;
    xml->setAttribute("presetName", name.trim());
    return xml->writeTo(dir.getChildFile(safe + ".mtpreset"));
}

bool MatterToneProcessor::loadUserPreset(const juce::File& presetFile)
{
    auto xml = juce::parseXML(presetFile);
    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType())) return false;
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    return true;
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
