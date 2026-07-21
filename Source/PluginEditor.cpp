// =============================================================================
//  MatterTone POC — PluginEditor.cpp
// =============================================================================
#include "PluginEditor.h"

// -----------------------------------------------------------------------------
void ScopeComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff10141c));
    g.fillRoundedRectangle(b, 4.0f);
    g.setColour(juce::Colour(0xff2a3340));
    g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);

    const float midY = b.getCentreY();
    g.setColour(juce::Colour(0xff223040));
    g.drawHorizontalLine((int) midY, b.getX(), b.getRight());

    if (data.size() > 1)
    {
        juce::Path path;
        const int w = (int) b.getWidth();
        const float h = b.getHeight() * 0.46f;
        const int   N = (int) data.size();
        for (int x = 0; x < w; ++x)
        {
            const int i = (int) ((double) x / w * (N - 1));
            const float y = midY - juce::jlimit(-1.0f, 1.0f, data[(size_t) i]) * h;
            if (x == 0) path.startNewSubPath(b.getX() + x, y);
            else        path.lineTo(b.getX() + x, y);
        }
        g.setColour(juce::Colour(0xff54d6b0));
        g.strokePath(path, juce::PathStrokeType(1.2f));
    }

    // level bar
    auto bar = b.removeFromBottom(6.0f).reduced(2.0f, 1.0f);
    g.setColour(juce::Colour(0xff1c2530));
    g.fillRect(bar);
    g.setColour(juce::Colour(0xffE0A040));
    g.fillRect(bar.withWidth((float) (bar.getWidth() * juce::jlimit(0.0, 1.0, peakLevel))));
}

// -----------------------------------------------------------------------------
MatterToneEditor::MatterToneEditor(MatterToneProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p),
      keyboard(p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    keyboard.setAvailableRange(36, 96);
    keyboard.setLowestVisibleKey(48);
    addAndMakeVisible(keyboard);
    titleLabel.setText("", juce::dontSendNotification);   // stealth beta: no plugin-title text
    titleLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(0xff54d6b0));
    addAndMakeVisible(titleLabel);

    auto presets = matter::factoryPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        presetBox.addItem(presets[(size_t) i].name, i + 1);
    presetBox.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this]
    {
        proc.setCurrentProgram(presetBox.getSelectedId() - 1);
    };
    presetBox.setTooltip("Factory presets (parameter states, never samples).");
    addAndMakeVisible(presetBox);

    resetBtn.onClick = [this] { proc.resetMaterialState(); };
    resetBtn.setTooltip("Reset the persistent material-memory state.");
    panicBtn.onClick = [this] { proc.panic(); };
    panicBtn.setTooltip("All notes off + reset all voice states.");
    addAndMakeVisible(resetBtn);
    addAndMakeVisible(panicBtn);

    for (auto* l : { &freqLabel, &energyLabel, &materialLabel, &statusLabel })
    {
        l->setColour(juce::Label::textColourId, juce::Colour(0xffb0c0d0));
        l->setFont(juce::Font(13.0f));
        addAndMakeVisible(*l);
    }
    addAndMakeVisible(scope);
   #if MATTER_USE_FCWT
    addAndMakeVisible(scalo);
    auto setupSlider = [this](juce::Slider& s, double lo, double hi, double def, double step, const juce::String& tip)
    {
        s.setSliderStyle(juce::Slider::LinearBar);
        s.setRange(lo, hi, step); s.setValue(def, juce::dontSendNotification);
        s.setColour(juce::Slider::trackColourId, juce::Colour(0xff2f5a4e));
        s.setTooltip(tip);
        s.onValueChange = [this]{ applyScaloConfig(); };
        addAndMakeVisible(s);
    };
    setupSlider(scBw,     0.5, 12.0,    2.0,  0.1, "Morlet bandwidth (mother-wavelet shape)");
    setupSlider(scF0,    20.0, 2000.0, 40.0,  1.0, "Lowest analysed frequency");
    setupSlider(scF1,   500.0, 20000.0,16000.0,10.0, "Highest analysed frequency");
    setupSlider(scScales,16.0, 128.0,  64.0,  1.0, "Number of scales");
    setupSlider(scCoi,    0.0, 1.0,     0.0,  0.001, "Crop cone-of-influence out of view (lookahead)");
    scScaleType.addItemList({ "Log scale", "Lin scale" }, 1); scScaleType.setSelectedId(1, juce::dontSendNotification);
    scScaleType.setTooltip("Log vs linear scale (frequency) spacing");
    scNorm.addItemList({ "L2 energy", "L1 amp" }, 1); scNorm.setSelectedId(1, juce::dontSendNotification);
    scNorm.setTooltip("L1 (amplitude) vs L2 (energy) coefficient normalization");
    scCoeff.addItemList({ "Coef lin", "Coef log", "Coef dB" }, 1); scCoeff.setSelectedId(1, juce::dontSendNotification);
    scCoeff.setTooltip("Display coefficients linearly, log, or in dB");
    for (auto* cb : { &scScaleType, &scNorm, &scCoeff }) { cb->onChange = [this]{ applyScaloConfig(); }; addAndMakeVisible(*cb); }
    scShowCoi.setToggleState(true, juce::dontSendNotification);
    scShowCoi.onClick = [this]{ applyScaloConfig(); };
    scShowCoi.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff90a0b0));
    addAndMakeVisible(scShowCoi);
    applyScaloConfig();
   #endif

    // ---- controls, grouped ----------------------------------------------------
    addSlider(pid::tension, "Tension", 0);
    addSlider(pid::nonlinStiff, "Nonlin Stiff", 0);
    addSlider(pid::hardening, "Hard/Soft", 0);
    addSlider(pid::damping, "Damping", 0);
    addSlider(pid::nonlinDamping, "Nonlin Damp", 0);
    addSlider(pid::coupling, "Coupling", 0);
    addSlider(pid::brittleness, "Brittleness", 0);
    addSlider(pid::adhesion, "Adhesion", 0);
    addSlider(pid::roughness, "Roughness", 0);
    addSlider(pid::healing, "Healing", 0);
    addSlider(pid::memory, "Memory", 0);
    addSlider(pid::contactHard, "Contact Hard", 0);
    addSlider(pid::contactLoss, "Contact Loss", 0);
    addSlider(pid::crackleCoup, "Crackle Coup", 0);

    addChoice(pid::excitation, "Excite", { "Pluck", "Load / Snap" }, 1);
    addSlider(pid::attack, "Attack", 1);
    addSlider(pid::pluckEnergy, "Pluck Energy", 1);
    addSlider(pid::loadingRate, "Loading Rate", 1);
    addSlider(pid::snapDepth, "Snap Depth", 1);
    addSlider(pid::strikePos, "Strike Pos", 1);
    addSlider(pid::velocitySens, "Velocity", 1);
    addSlider(pid::agitation, "Agitation", 1);

    addChoice(pid::tuningMode, "Tuning", { "Chromatic", "Ratio Set", "Attractor Scale" }, 2);
    addSlider(pid::rootNote, "Root Note", 2);
    addSlider(pid::modeRatio, "Mode Ratio", 2);
    addSlider(pid::attractorSet, "Attractor Set", 2);
    addSlider(pid::scaleSpread, "Scale Spread", 2);
    addSlider(pid::energyToPitch, "Energy>Pitch", 2);
    addSlider(pid::stabilityBias, "Stability", 2);

    addSlider(pid::output, "Output", 3);
    addSlider(pid::numVoices, "Polyphony", 3);
    addChoice(pid::oversampling, "Oversample", { "2x", "4x", "8x" }, 3);
    addSlider(pid::seed, "Seed", 3);
    addSlider(pid::bodyMix, "Body Mix", 3);
    addSlider(pid::eventMix, "Event Mix", 3);

    setSize(1120, 760);
    startTimerHz(30);
}

void MatterToneEditor::addSlider(const char* id, const juce::String& name, int group)
{
    Ctrl c;
    c.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    c.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
    c.slider->setColour(juce::Slider::trackColourId, juce::Colour(0xff54d6b0));
    addAndMakeVisible(*c.slider);
    c.label = std::make_unique<juce::Label>();
    c.label->setText(name, juce::dontSendNotification);
    c.label->setFont(juce::Font(12.0f));
    c.label->setColour(juce::Label::textColourId, juce::Colour(0xff90a0b0));
    addAndMakeVisible(*c.label);
    c.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state(), id, *c.slider);
    c.group = group;
    controls.push_back(std::move(c));
}

void MatterToneEditor::addChoice(const char* id, const juce::String& name, juce::StringArray items, int group)
{
    Choice c;
    c.box = std::make_unique<juce::ComboBox>();
    c.box->addItemList(items, 1);
    addAndMakeVisible(*c.box);
    c.label = std::make_unique<juce::Label>();
    c.label->setText(name, juce::dontSendNotification);
    c.label->setFont(juce::Font(12.0f));
    c.label->setColour(juce::Label::textColourId, juce::Colour(0xff90a0b0));
    addAndMakeVisible(*c.label);
    c.att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.state(), id, *c.box);
    c.group = group;
    choices.push_back(std::move(c));
}

// -----------------------------------------------------------------------------
void MatterToneEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0b0e14));
    // group headers
    g.setColour(juce::Colour(0xff54d6b0));
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    for (int gi = 0; gi < kGroups; ++gi)
        g.drawText(groupNames[gi], groupHeaderX[gi], groupHeaderY, 200, 16,
                   juce::Justification::centredLeft);
}

void MatterToneEditor::resized()
{
    auto area = getLocalBounds().reduced(8);

    keyboard.setBounds(area.removeFromBottom(66));   // playable on-screen keyboard
    area.removeFromBottom(6);

    auto top = area.removeFromTop(30);
    titleLabel.setBounds(top.removeFromLeft(150));
    top.removeFromLeft(8);
    presetBox.setBounds(top.removeFromLeft(230));
    top.removeFromLeft(8);
    panicBtn.setBounds(top.removeFromRight(80));
    top.removeFromRight(6);
    resetBtn.setBounds(top.removeFromRight(120));

    auto info = area.removeFromTop(22);
    const int iw = info.getWidth() / 4;
    freqLabel.setBounds(info.removeFromLeft(iw));
    energyLabel.setBounds(info.removeFromLeft(iw));
    materialLabel.setBounds(info.removeFromLeft(iw));
    statusLabel.setBounds(info);

    auto display = area.removeFromTop(150);
   #if MATTER_USE_FCWT
    scope.setBounds(display.removeFromLeft(display.getWidth() * 42 / 100).reduced(0, 0));
    display.removeFromLeft(6);
    scalo.setBounds(display);
   #else
    scope.setBounds(display);
   #endif
    area.removeFromTop(8);

   #if MATTER_USE_FCWT
    {
        auto sc = area.removeFromTop(24);
        auto cell = [&](int w){ auto r = sc.removeFromLeft(w); sc.removeFromLeft(4); return r; };
        scBw.setBounds(cell(84)); scF0.setBounds(cell(84)); scF1.setBounds(cell(92));
        scScales.setBounds(cell(72)); scScaleType.setBounds(cell(94)); scNorm.setBounds(cell(84));
        scCoeff.setBounds(cell(84)); scCoi.setBounds(cell(110)); scShowCoi.setBounds(cell(56));
    }
    area.removeFromTop(6);
   #endif

    groupHeaderY = area.getY();
    const int colW = area.getWidth() / kGroups;
    for (int gi = 0; gi < kGroups; ++gi)
    {
        auto col = area.removeFromLeft(colW).reduced(4, 0);
        groupHeaderX[gi] = col.getX();
        col.removeFromTop(20); // header space

        auto placeRow = [&](juce::Label& lab, juce::Component& ctrl)
        {
            auto row = col.removeFromTop(26);
            lab.setBounds(row.removeFromLeft(92));
            ctrl.setBounds(row.reduced(0, 2));
        };
        for (auto& c : choices)  if (c.group == gi) placeRow(*c.label, *c.box);
        for (auto& c : controls) if (c.group == gi) placeRow(*c.label, *c.slider);
    }
}

// -----------------------------------------------------------------------------
#if MATTER_USE_FCWT
void MatterToneEditor::applyScaloConfig()
{
    matter::ScalogramConfig c;
    c.windowSize = 2048;
    c.bandwidth  = (float) scBw.getValue();
    c.f0         = (float) scF0.getValue();
    c.f1         = (float) scF1.getValue();
    c.numScales  = (int)   scScales.getValue();
    c.logScales  = (scScaleType.getSelectedId() == 1);
    c.l1norm     = (scNorm.getSelectedId() == 2);
    scalo.setEngineConfig(c);

    ScalogramComponent::View v;
    v.magMode  = scCoeff.getSelectedId() - 1;   // 0 lin, 1 log, 2 dB
    v.coiCrop  = (float) scCoi.getValue();
    v.showCoi  = scShowCoi.getToggleState();
    scalo.setView(v);
}
#endif

void MatterToneEditor::timerCallback()
{
    std::vector<float> buf;
    proc.copyScope(buf);

    double peak = 0.0, sumsq = 0.0;
    for (float s : buf) { const double a = std::fabs(s); if (a > peak) peak = a; sumsq += (double) s * s; }
    const double rms = buf.empty() ? 0.0 : std::sqrt(sumsq / buf.size());

    scope.setData(buf, rms, peak);
   #if MATTER_USE_FCWT
    scalo.update(buf, proc.currentSampleRate());
   #endif

    // Pitch estimate is heavier — refresh a few times per second only.
    if (++pitchTick >= 6)
    {
        pitchTick = 0;
        lastFreq = proc.measuredFrequency();
    }

    freqLabel.setText(lastFreq > 0.0 ? juce::String("Pitch: ") + juce::String(lastFreq, 1) + " Hz"
                                     : juce::String("Pitch: --"), juce::dontSendNotification);
    energyLabel.setText("Level: " + juce::String(rms, 4), juce::dontSendNotification);
    materialLabel.setText("Material: " + juce::String(proc.materialState(), 2), juce::dontSendNotification);
    statusLabel.setText("Voices: " + juce::String(proc.activeVoices())
                        + "   Resets: " + juce::String(proc.resetCount()),
                        juce::dontSendNotification);
}

// =============================================================================
#if MATTER_USE_FCWT
static juce::Colour magma(float t)
{
    t = juce::jlimit(0.0f, 1.0f, t);
    static const float a[5][3] = {
        {  0,   0,   4 }, { 60,  15, 110 }, { 140, 40, 120 }, { 230, 80, 60 }, { 252, 220, 120 } };
    const float x = t * 4.0f;
    const int   i = juce::jmin(3, (int) x);
    const float f = x - i;
    auto lerp = [&](int c) { return a[i][c] + (a[i + 1][c] - a[i][c]) * f; };
    return juce::Colour::fromRGB((juce::uint8) lerp(0), (juce::uint8) lerp(1), (juce::uint8) lerp(2));
}

void ScalogramComponent::update(const std::vector<float>& scope, double sampleRate)
{
    if ((int) scope.size() < pending.windowSize) return;

    if (!engine.ready() || preparedSr != sampleRate || applied != pending)
    {
        engine.prepare(sampleRate, pending);
        applied = pending; preparedSr = sampleRate;
    }
    engine.process(scope.data(), (int) scope.size());

    const int nS = engine.numScales();
    const int win = engine.windowSize();
    if (nS < 2 || win < 2) return;

    const int imgW = 256, imgH = nS;
    if (!image.isValid() || image.getWidth() != imgW || image.getHeight() != imgH)
        image = juce::Image(juce::Image::RGB, imgW, imgH, false);

    const auto& m = engine.magnitudes();
    const bool ascending = engine.freqOf(0) < engine.freqOf(nS - 1);

    juce::Image::BitmapData bmp(image, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < imgH; ++y)
    {
        const int s = ascending ? (nS - 1 - y) : y;    // high frequency at the top
        for (int x = 0; x < imgW; ++x)
        {
            const int t0 = x * win / imgW;
            const int t1 = juce::jmax(t0 + 1, (x + 1) * win / imgW);
            float mx = 0.0f;
            for (int t = t0; t < t1 && t < win; ++t)
                mx = juce::jmax(mx, m[(size_t) s * win + t]);
            // coefficient display transform
            float v;
            if (view.magMode == 1)                                       // log coefficients
                v = std::log1p(mx * 40.0f) / std::log1p(40.0f);
            else if (view.magMode == 2)                                  // dB coefficients
            { const float db = 20.0f * std::log10(mx + 1.0e-5f); v = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); }
            else                                                         // linear (gamma)
                v = std::pow(mx, 0.6f);
            bmp.setPixelColour(x, y, magma(v));
        }
    }
    haveData = true;
    repaint();
}

void ScalogramComponent::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff05060a));
    g.fillRoundedRectangle(b, 4.0f);

    if (haveData && image.isValid() && engine.ready())
    {
        const int nS = engine.numScales();
        const int win = engine.windowSize();
        const double sr = engine.sampleRate();
        const bool ascending = engine.freqOf(0) < engine.freqOf(nS - 1);
        const float fLow = juce::jmax(1.0f, ascending ? engine.freqOf(0) : engine.freqOf(nS - 1));

        // Cone of influence half-width ~ one wavelength (samples), widest at fLow.
        const double coiConst = 1.0;
        const double maxW = coiConst * sr / fLow;
        const double crop = (double) view.coiCrop * maxW;      // "lookahead / cutoff"
        const double t0 = juce::jlimit(0.0, win * 0.45, crop * 0.4);         // small left trim
        const double t1 = juce::jlimit(t0 + 1.0, (double) win, (double) win - crop); // right (recent) trim

        const int imgW = image.getWidth();
        const int c0 = juce::jlimit(0, imgW - 1, (int) (t0 / win * imgW));
        const int c1 = juce::jlimit(c0 + 1, imgW, (int) (t1 / win * imgW));
        auto sub = image.getClippedImage(juce::Rectangle<int>(c0, 0, c1 - c0, image.getHeight()));
        auto dst = b.reduced(1.0f);
        g.drawImage(sub, dst, juce::RectanglePlacement::stretchToFit);

        if (view.showCoi)                                      // shade the remaining cone
        {
            const float W = dst.getWidth(), H = dst.getHeight(), X = dst.getX(), Y = dst.getY();
            auto cx = [&](double t){ return X + (float) ((t - t0) / (t1 - t0)) * W; };
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            for (int y = 0; y < nS; ++y)
            {
                const int s = ascending ? (nS - 1 - y) : y;
                const double w = coiConst * sr / juce::jmax(1.0f, engine.freqOf(s));
                const float ry = Y + (float) y / nS * H, rh = H / nS + 1.0f;
                if (w > t0)      { const float xw = cx(juce::jmin((double) win, w));  if (xw > X)      g.fillRect(juce::Rectangle<float>(X, ry, xw - X, rh)); }
                if (win - w < t1){ const float xr = cx(juce::jmax(t0, (double) win - w)); if (xr < X + W) g.fillRect(juce::Rectangle<float>(xr, ry, X + W - xr, rh)); }
            }
        }
    }

    g.setColour(juce::Colour(0xff2a3340));
    g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::Font(11.0f));
    const char* mm = view.magMode == 1 ? "log" : (view.magMode == 2 ? "dB" : "lin");
    g.drawText(juce::String("CWT (fCWT) · ") + mm, getLocalBounds().reduced(4), juce::Justification::topLeft);
    g.drawText("hi f", getLocalBounds().reduced(4), juce::Justification::topRight);
    g.drawText("lo f", getLocalBounds().reduced(4), juce::Justification::bottomRight);
}
#endif // MATTER_USE_FCWT
