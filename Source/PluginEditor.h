// =============================================================================
//  MatterTone POC — PluginEditor.h
//  A functional (not fancy) editor: preset selector, grouped parameter panels,
//  a real-time waveform scope, a stored-energy meter, a material-state readout,
//  the measured dominant frequency after onset, and reset / panic buttons.
//  (The foleys_gui_magic build replaces this with a WYSIWYG-editable layout.)
// =============================================================================
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

#if MATTER_USE_FCWT
 #include "ui/ScalogramEngine.h"
#endif

// Real-time waveform + level scope, fed from the processor's rolling buffer.
class ScopeComponent : public juce::Component
{
public:
    void setData(const std::vector<float>& d, double energy, double peak)
    {
        data = d; energyLevel = energy; peakLevel = peak; repaint();
    }
    void paint(juce::Graphics& g) override;
private:
    std::vector<float> data;
    double energyLevel = 0.0, peakLevel = 0.0;
};

#if MATTER_USE_FCWT
// Real-time fCWT scalogram: |CWT| heatmap of the recent output (UI thread only).
class ScalogramComponent : public juce::Component
{
public:
    struct View { int magMode = 0;       // 0 linear, 1 log, 2 dB coefficients
                  float coiCrop = 0.0f;  // crop the cone-of-influence out of view (0..1)
                  bool  showCoi = true; };

    void setEngineConfig(const matter::ScalogramConfig& c) { pending = c; }
    void setView(const View& v) { view = v; repaint(); }
    void update(const std::vector<float>& scope, double sampleRate);
    void paint(juce::Graphics& g) override;
private:
    matter::ScalogramEngine engine;
    matter::ScalogramConfig pending {}, applied {};
    View view;
    juce::Image image;
    double preparedSr = 0.0;
    bool   haveData = false;
};
#endif

#if MATTER_USE_FOLEYS && MATTER_USE_FCWT
// Makes the fCWT scalogram a first-class foleys widget: it appears in the
// WYSIWYG palette and can be dropped into the editable layout. It polls the
// processor's rolling output on a UI timer (never the audio thread).
class ScalogramItem : public foleys::GuiItem, private juce::Timer
{
public:
    ScalogramItem(foleys::MagicGUIBuilder& b, const juce::ValueTree& node)
        : foleys::GuiItem(b, node)
    {
        addAndMakeVisible(scalo);
        proc = dynamic_cast<MatterToneProcessor*>(b.getMagicState().getProcessor());
        startTimerHz(30);
    }
    ~ScalogramItem() override { stopTimer(); }

    juce::Component* getWrappedComponent() override { return &scalo; }
    void update() override {}

    static std::unique_ptr<foleys::GuiItem> factory(foleys::MagicGUIBuilder& b, const juce::ValueTree& node)
    {
        return std::make_unique<ScalogramItem>(b, node);
    }

private:
    void timerCallback() override
    {
        if (proc == nullptr) return;
        std::vector<float> buf;
        proc->copyScope(buf);
        scalo.update(buf, proc->currentSampleRate());
    }
    ScalogramComponent   scalo;
    MatterToneProcessor* proc = nullptr;
};
#endif

class MatterToneEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit MatterToneEditor(MatterToneProcessor&);
    ~MatterToneEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Ctrl
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label>  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        int group = 0;
    };
    struct Choice
    {
        std::unique_ptr<juce::ComboBox> box;
        std::unique_ptr<juce::Label>    label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
        int group = 0;
    };

    void addSlider(const char* id, const juce::String& name, int group);
    void addChoice(const char* id, const juce::String& name, juce::StringArray items, int group);

    MatterToneProcessor& proc;

    juce::MidiKeyboardComponent keyboard;
    juce::ComboBox  presetBox;
    juce::TextButton resetBtn { "Reset Material" }, panicBtn { "Panic" };
    juce::Label     titleLabel, freqLabel, energyLabel, materialLabel, statusLabel;
    ScopeComponent  scope;
   #if MATTER_USE_FCWT
    ScalogramComponent scalo;
    juce::Slider   scBw, scF0, scF1, scScales, scCoi;
    juce::ComboBox scScaleType, scNorm, scCoeff;
    juce::ToggleButton scShowCoi { "COI" };
    void applyScaloConfig();
   #endif

    std::vector<Ctrl>   controls;
    std::vector<Choice> choices;
    juce::TooltipWindow tooltips { this, 500 };

    static constexpr int kGroups = 4;
    const char* groupNames[kGroups] { "Material", "Excitation", "Dynamics / Tone", "Output / Global" };
    int    groupHeaderX[kGroups] { 0, 0, 0, 0 };
    int    groupHeaderY = 0;
    int    pitchTick = 0;
    double lastFreq  = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatterToneEditor)
};
