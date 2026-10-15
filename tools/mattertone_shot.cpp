// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// =============================================================================
//  MatterTone II — mattertone_shot
//  Offscreen UI screenshot harness. Links the juce_add_plugin shared-code
//  library, instantiates MatterToneProcessor + its foleys editor, drives real
//  audio/MIDI through processBlock so the scope / scalogram / meters have
//  content, and writes one PNG per UI state via Component::createComponentSnapshot.
//
//  NOTHING here is part of the plugin build: no bundle target is touched, no
//  DSP/GUI source is modified. Run:
//      mattertone_shot [outputDir] [stateIndex]
//      mattertone_shot --selftest      operate every control, check the wiring
//  MATTERTONE_SHOT_SIZE=WxH renders every state at that size (e.g. the minimum).
//  With no stateIndex the process re-spawns itself once per state (keeps each
//  child well under foleys' 10 s ToolBox autosave timer and isolates crashes).
// =============================================================================

#include "PluginProcessor.h"
#include "WiringTest.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <CoreFoundation/CoreFoundation.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

namespace
{

// default output: ./shots under the current directory (pass a path to change it)
static juce::String defaultOutDir() { return juce::File::getCurrentWorkingDirectory().getChildFile ("shots").getFullPathName(); }

// juce::MessageManager::runDispatchLoopUntil() is compiled out unless
// JUCE_MODAL_LOOPS_PERMITTED=1 (it is not, and we must not change the plugin's
// build). CFRunLoopRunInMode dispatches JUCE messages and fires JUCE timers
// identically on macOS.
void pump (double seconds)
{
    CFRunLoopRunInMode (kCFRunLoopDefaultMode, seconds, false);
}

// -----------------------------------------------------------------------------
struct Driver
{
    MatterToneProcessor&           proc;
    juce::AudioProcessorEditor*    editor = nullptr;
    juce::AudioBuffer<float>       buf { 2, 512 };
    juce::MidiBuffer               midi;

    void noteOn (int note, float vel)
    {
        midi.addEvent (juce::MidiMessage::noteOn (1, note, vel), 0);
        if (auto* me = dynamic_cast<foleys::MagicPluginEditor*> (editor))
            me->getGUIBuilder().getMagicState().getKeyboardState().noteOn (1, note, vel);
    }

    void noteOff (int note)
    {
        midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        if (auto* me = dynamic_cast<foleys::MagicPluginEditor*> (editor))
            me->getGUIBuilder().getMagicState().getKeyboardState().noteOff (1, note, 0.0f);
    }

    // processBlock clears the audio buffer itself; the MidiBuffer is ours to clear.
    void blocks (int n)
    {
        for (int i = 0; i < n; ++i)
        {
            proc.processBlock (buf, midi);
            midi.clear();
        }
    }
};

void setP (MatterToneProcessor& proc, const char* id, float raw)
{
    if (auto* p = proc.state().getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (raw));
    else
        std::printf ("WARN unknown parameter id '%s'\n", id);
}

// foleys' GuiItem binds `visibility="showModal|showPerc"` by Value::referTo() at
// GUI-build time but only calls setVisible() from valueChanged() — i.e. the bound
// property is applied on CHANGE, never on the initial build. So after the editor
// exists we flip the engine parameter to the other value and back: that makes both
// magicState properties transition and the mode-dependent fasciae settle correctly.
// (No plugin source is touched — this is purely host-side parameter automation.)
void forceVisibilityRefresh (MatterToneProcessor& proc)
{
    auto* engine = proc.state().getParameter (pid::engine);
    if (engine == nullptr) return;

    const float wanted = engine->convertFrom0to1 (engine->getValue());
    const float other  = (wanted >= 0.5f ? 0.0f : 1.0f);

    setP (proc, pid::engine, other);   pump (0.12);
    setP (proc, pid::engine, wanted);  pump (0.12);
}

juce::TabbedButtonBar* findPages (juce::Component& c)
{
    if (auto* bar = dynamic_cast<juce::TabbedButtonBar*> (&c)) return bar;
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        if (auto* bar = findPages (*c.getChildComponent (i))) return bar;
    return nullptr;
}

void showPage (Driver& d, int index)
{
    if (d.editor == nullptr) return;
    if (auto* pages = findPages (*d.editor))
    {
        pages->setCurrentTabIndex (index);
        pump (0.3);
    }
}

// -----------------------------------------------------------------------------
struct Shot
{
    const char* slug;
    std::function<void (MatterToneProcessor&)> configure;  // BEFORE createEditor()
    std::function<void (Driver&)>              drive;      // AFTER  createEditor()
    bool allowLowVariance = false;                         // genuine empty state
};

std::vector<Shot> makeShots()
{
    std::vector<Shot> s;

    // 01 — cold idle: factory defaults, modal engine, no MIDI at all.
    s.push_back ({ "cold-idle",
        [] (MatterToneProcessor&) {},
        [] (Driver& d) { d.blocks (8); },
        true });

    // 02 — modal chord: "Stable Nonlinear Bell", sustained tonal ring.
    s.push_back ({ "modal-chord",
        [] (MatterToneProcessor& p) { p.setCurrentProgram (6); },
        [] (Driver& d)
        {
            d.noteOn (45, 0.9f); d.noteOn (52, 0.9f);
            d.noteOn (57, 0.9f); d.noteOn (64, 0.9f);
            d.blocks (1);
            d.blocks (40);                        // ~427 ms into the steady ring
        } });

    // 03 — percussion kick: whole layout swaps (Percussion fascia appears).
    s.push_back ({ "percussion-kick",
        [] (MatterToneProcessor& p) { p.setCurrentProgram (10); },
        [] (Driver& d)
        {
            d.noteOn (36, 1.0f); d.blocks (1); d.blocks (19);
            d.noteOff (36);
            d.noteOn (36, 1.0f); d.blocks (1); d.blocks (3);   // last 2048 = attack
        } });

    // 04 — wire snare: percussion, broadband noise wash, 8x OS, many modes.
    s.push_back ({ "wire-snare",
        [] (MatterToneProcessor& p)
        {
            p.setCurrentProgram (12);
            setP (p, pid::oversampling, 2.0f);   // 8x
            setP (p, pid::membraneModes, 18.0f);
            setP (p, pid::wireAmount, 0.75f);
        },
        [] (Driver& d)
        {
            d.noteOn (50, 0.95f); d.blocks (1); d.blocks (15);
            d.noteOff (50);
            d.noteOn (50, 0.95f); d.blocks (1); d.blocks (3);
        } });

    // 05 — crackle / load-snap: modal engine, Events fascia at extremes.
    s.push_back ({ "crackle-loadsnap",
        [] (MatterToneProcessor& p)
        {
            p.setCurrentProgram (4);
            setP (p, pid::roughness, 0.85f);
            setP (p, pid::crackleCoup, 0.85f);
        },
        [] (Driver& d)
        {
            d.noteOn (40, 0.85f); d.blocks (1); d.blocks (90);   // let it load + snap
            d.blocks (4);
        } });

    // 06 — LFO wired: modulation section fully non-default.
    s.push_back ({ "lfo-wired",
        [] (MatterToneProcessor& p)
        {
            setP (p, pid::lfoAShape,   5.0f);    // Sample & Hold
            setP (p, pid::lfoASync,    1.0f);
            setP (p, pid::lfoARetrig,  1.0f);    // Key
            setP (p, pid::driveModA,   0.85f);
            setP (p, pid::dampModA,   -0.55f);
            setP (p, pid::crackleModA, 0.40f);
            setP (p, pid::lfoBShape,   6.0f);    // Smooth Random
            setP (p, pid::lfoBRateHz,  0.25f);
            setP (p, pid::coupModB,   -0.70f);
            setP (p, pid::driveModB,   0.30f);
        },
        [] (Driver& d)
        {
            showPage (d, 1);
            d.noteOn (45, 0.9f); d.noteOn (52, 0.9f); d.noteOn (57, 0.9f);
            d.blocks (1);
            d.blocks (40);
        } });

    // 07 — compact layout: modal chord reflowed at the declared minimum size.
    s.push_back ({ "compact-min-size",
        [] (MatterToneProcessor& p) { p.setCurrentProgram (6); },
        [] (Driver& d)
        {
            if (d.editor != nullptr) d.editor->setSize (1100, 760);
            d.noteOn (45, 0.9f); d.noteOn (52, 0.9f); d.noteOn (57, 0.9f);
            d.blocks (1);
            d.blocks (40);
        } });

    // 08 — every time set in beats or as an absolute time (the swapped controls).
    s.push_back ({ "timed-and-beats",
        [] (MatterToneProcessor& p)
        {
            p.setCurrentProgram (4);
            setP (p, pid::excitation, 1.0f);     // load / snap
            setP (p, pid::attackOvr, 1.0f);      // attack: absolute time
            setP (p, pid::loadOvr, 1.0f);  setP (p, pid::loadSync, 1.0f);   // load: beats
        },
        [] (Driver& d)
        {
            d.noteOn (40, 0.85f); d.blocks (1); d.blocks (60);
        } });

    // 09 — the second page at the minimum size, LFO rates in beats.
    s.push_back ({ "motion-page-min-size",
        [] (MatterToneProcessor& p)
        {
            p.setCurrentProgram (6);
            setP (p, pid::lfoASync, 1.0f);  setP (p, pid::lfoBSync, 1.0f);
            setP (p, pid::driveModA, 0.5f); setP (p, pid::coupModB, -0.4f);
        },
        [] (Driver& d)
        {
            if (d.editor != nullptr) d.editor->setSize (1100, 760);
            showPage (d, 1);
            d.noteOn (45, 0.9f); d.noteOn (52, 0.9f);
            d.blocks (1);
            d.blocks (40);
        } });

    return s;
}

// -----------------------------------------------------------------------------
// Cheap non-blankness metric: stddev of 8-bit luma over a subsampled grid,
// plus a count of distinct luma buckets.
struct ImageStats { double stddev = 0.0; int buckets = 0; };

ImageStats measure (const juce::Image& img, int yFrom = 0)
{
    ImageStats st;
    if (! img.isValid()) return st;

    const int w = img.getWidth(), h = img.getHeight();
    const int step = juce::jmax (1, juce::jmin (w, h - yFrom) / 64);

    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    std::vector<int> hist (256, 0);
    double sum = 0.0, sum2 = 0.0; long long n = 0;

    for (int y = yFrom; y < h; y += step)
        for (int x = 0; x < w; x += step)
        {
            const auto c = bd.getPixelColour (x, y);
            const double l = 0.2126 * c.getRed() + 0.7152 * c.getGreen() + 0.0722 * c.getBlue();
            sum += l; sum2 += l * l; ++n;
            hist[(size_t) juce::jlimit (0, 255, (int) l)]++;
        }

    if (n > 0)
    {
        const double mean = sum / (double) n;
        st.stddev = std::sqrt (juce::jmax (0.0, sum2 / (double) n - mean * mean));
    }
    for (int i = 0; i < 256; ++i) if (hist[(size_t) i] > 0) ++st.buckets;
    return st;
}

// -----------------------------------------------------------------------------
int runOne (const Shot& shot, int index, const juce::File& outDir)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    int result = 0;
    {
        MatterToneProcessor proc;

        // 1) Select preset / params FIRST so the engine AsyncUpdater has set the
        //    magicState showModal/showPerc properties before the GUI is built.
        shot.configure (proc);
        pump (0.15);

        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);

        // 2) Build the editor (size comes straight off magic.xml: 1140x780).
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        if (ed == nullptr)
        {
            std::printf ("FAIL %s — createEditor() returned null\n", shot.slug);
            return 1;
        }
        ed->setVisible (true);

        // foleys builds the GUI, timers start, fCWT allocates FFTW plans on the
        // first ScalogramItem tick — pump generously here.
        pump (0.5);
        pump (0.3);

        if (auto* size = std::getenv ("MATTERTONE_SHOT_SIZE"))
        {
            const auto wh = juce::StringArray::fromTokens (juce::String (size), "x", {});
            if (wh.size() == 2) ed->setSize (wh[0].getIntValue(), wh[1].getIntValue());
            pump (0.3);
        }

        if (ed->getWidth() <= 0 || ed->getHeight() <= 0)
        {
            std::printf ("FAIL %s — editor has zero size\n", shot.slug);
            return 1;
        }

        // 3) Settle the mode-dependent fascia visibility (see above).
        forceVisibilityRefresh (proc);
        proc.panic();

        // 4) Drive audio / MIDI.
        Driver d { proc, ed.get(), juce::AudioBuffer<float> (2, 512), {} };
        shot.drive (d);

        // 4) Let the 30 Hz scalogram timer fire at least once, then repaint.
        pump (0.35);
        pump (0.2);

        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);

        juce::File out = outDir.getChildFile (juce::String::formatted ("%02d_", index + 1) + shot.slug + ".png");
        out.deleteFile();
        {
            juce::FileOutputStream os (out);
            if (os.failedToOpen())
            {
                std::printf ("FAIL %s — cannot open %s\n", shot.slug, out.getFullPathName().toRawUTF8());
                return 1;
            }
            juce::PNGImageFormat png;
            png.writeImageToStream (img, os);
            os.flush();
        }

        // Blank check. For the genuine empty state only look below the hero
        // scalogram (fCWT normalises a silent buffer to near-black by design).
        const auto full = measure (img);
        const auto body = measure (img, juce::jmin (img.getHeight() - 1, 400));
        const double v = shot.allowLowVariance ? body.stddev : full.stddev;

        std::printf ("STATE %s %dx%d variance=%.3f buckets=%d bodyVariance=%.3f file=%s bytes=%lld\n",
                     shot.slug, img.getWidth(), img.getHeight(),
                     full.stddev, full.buckets, body.stddev,
                     out.getFullPathName().toRawUTF8(), (long long) out.getSize());

        if (v < 4.0 || (shot.allowLowVariance ? body.buckets : full.buckets) < 8)
        {
            std::printf ("FAIL %s — image is effectively uniform (variance=%.3f)\n", shot.slug, v);
            result = 1;
        }
        else
        {
            std::printf ("OK %s\n", shot.slug);
        }

        ed.reset();
    }

    // Belt and braces against foleys' ToolBox autosave dropping a file in the repo.
    juce::File (__FILE__).getParentDirectory().getParentDirectory()
        .getChildFile ("Resources").getChildFile ("magic.sav.xml").deleteFile();

    return result;
}

// -----------------------------------------------------------------------------
//  Control wiring self-test (--selftest). The fascia has pages: every page is
//  walked in each state, so a control only has to be on screen somewhere.
// -----------------------------------------------------------------------------
int runSelfTest()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    int rc = 1;
    {
        MatterToneProcessor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        if (ed == nullptr) { std::printf ("FAIL no editor\n"); return 1; }
        ed->setVisible (true);
        pump (0.9);

        if (auto* size = std::getenv ("MATTERTONE_SHOT_SIZE"))
        {
            const auto wh = juce::StringArray::fromTokens (juce::String (size), "x", {});
            if (wh.size() == 2) ed->setSize (wh[0].getIntValue(), wh[1].getIntValue());
            pump (0.3);
        }
        std::printf ("editor size: %d x %d\n", ed->getWidth(), ed->getHeight());

        acv2::test::WiringTest test { proc, [] (int ms) { pump (0.001 * (double) ms); }, {}, 0 };

        auto everyPage = [&] (const juce::String& state)
        {
            auto* pages = findPages (*ed);
            const int n = pages != nullptr ? pages->getNumTabs() : 1;
            for (int i = 0; i < n; ++i)
            {
                juce::String label = state;
                if (pages != nullptr)
                {
                    pages->setCurrentTabIndex (i);
                    pump (0.3);
                    label << ", page '" << pages->getTabNames()[i] << "'";
                }
                test.run (*ed, label.toRawUTF8());
            }
            if (pages != nullptr) { pages->setCurrentTabIndex (0); pump (0.2); }
        };

        everyPage ("modal engine, free times");

        setP (proc, pid::excitation, 1.0f);                    // load / snap
        setP (proc, pid::attackOvr, 1.0f);
        setP (proc, pid::loadOvr, 1.0f);
        pump (0.4);
        everyPage ("modal engine, load and snap, absolute times");

        for (auto* id : { pid::attackOvr, pid::attackSync, pid::loadOvr, pid::loadSync,
                          pid::lfoASync, pid::lfoBSync })
            setP (proc, id, 1.0f);
        setP (proc, pid::excitation, 1.0f);                    // load / snap
        pump (0.4);
        everyPage ("modal engine, load and snap, times in beats");

        setP (proc, pid::engine, 1.0f);
        pump (0.4);
        everyPage ("percussion engine");

        // what a knob says, and what typing into it does
        std::printf ("  [readouts]\n");
        auto textAt = [&] (const char* id, float plain)
        {
            auto* p = proc.state().getParameter (id);
            return p->getText (p->convertTo0to1 (plain), 64);
        };
        auto typed = [&] (const char* id, const char* text)
        {
            auto* p = proc.state().getParameter (id);
            return p->convertFrom0to1 (p->getValueForText (text));
        };
        auto expect = [&] (bool ok, const juce::String& what)
        {
            if (ok) return;
            ++test.failures;
            std::printf ("  FAIL %s\n", what.toRawUTF8());
        };
        expect (textAt (pid::tension, 1.0f) == "1.00",  "tension at 1 reads '1.00', got '" + textAt (pid::tension, 1.0f) + "'");
        expect (textAt (pid::damping, 0.06f) == "6 %",  "damping at 0.06 reads '6 %', got '" + textAt (pid::damping, 0.06f) + "'");
        expect (textAt (pid::hardening, -0.4f) == "-0.40", "hardening at -0.4 reads '-0.40', got '" + textAt (pid::hardening, -0.4f) + "'");
        expect (textAt (pid::output, 0.7f) == "0.70",   "level at 0.7 reads '0.70', got '" + textAt (pid::output, 0.7f) + "'");
        expect (textAt (pid::driveModA, 0.0f) == "0 %", "a depth at rest reads '0 %', got '" + textAt (pid::driveModA, 0.0f) + "'");
        expect (std::abs (typed (pid::damping, "50 %") - 0.5f) < 1.0e-3f,  "typing '50 %' into damping gives 0.5");
        expect (std::abs (typed (pid::damping, "50") - 0.5f) < 1.0e-3f,    "typing '50' into damping gives 0.5");
        expect (std::abs (typed (pid::agc, "25 %") - 0.25f) < 1.0e-3f,     "typing '25 %' into attack AGC gives 0.25");
        expect (std::abs (typed (pid::driveModA, "-30 %") + 0.3f) < 1.0e-3f, "typing '-30 %' into a depth gives -0.3");
        expect (std::abs (typed (pid::tension, "2.5") - 2.5f) < 1.0e-3f,   "typing '2.5' into tension gives 2.5");
        expect (std::abs (typed (pid::attackMs, "20 ms") - 20.0f) < 0.1f,  "typing '20 ms' into the attack time gives 20 ms");

        rc = test.report();
        ed.reset();
    }

    juce::File (__FILE__).getParentDirectory().getParentDirectory()
        .getChildFile ("Resources").getChildFile ("magic.sav.xml").deleteFile();
    return rc;
}

} // namespace

// -----------------------------------------------------------------------------
int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);     // keep the log if the run is killed
    if (argc > 1 && juce::String (argv[1]) == "--selftest")
        return runSelfTest();

    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : defaultOutDir());
    outDir.createDirectory();

    const auto shots = makeShots();

    if (argc > 2)
    {
        const int idx = juce::String (argv[2]).getIntValue();
        if (idx < 0 || idx >= (int) shots.size())
        {
            std::printf ("FAIL — state index %d out of range (0..%d)\n", idx, (int) shots.size() - 1);
            return 2;
        }
        return runOne (shots[(size_t) idx], idx, outDir);
    }

    // Parent mode: one child process per state.
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    int failures = 0;
    for (int i = 0; i < (int) shots.size(); ++i)
    {
        juce::ChildProcess cp;
        juce::StringArray args { exe.getFullPathName(), outDir.getFullPathName(), juce::String (i) };
        if (! cp.start (args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        {
            std::printf ("FAIL %s — could not spawn child\n", shots[(size_t) i].slug);
            ++failures;
            continue;
        }
        const auto output = cp.readAllProcessOutput();
        std::fputs (output.toRawUTF8(), stdout);
        const int rc = (int) cp.getExitCode();
        if (rc != 0)
        {
            std::printf ("FAIL %s — child exit code %d\n", shots[(size_t) i].slug, rc);
            ++failures;
        }
        std::fflush (stdout);
    }

    std::printf ("\n=== %d/%d states rendered OK -> %s ===\n",
                 (int) shots.size() - failures, (int) shots.size(),
                 outDir.getFullPathName().toRawUTF8());
    return failures == 0 ? 0 : 1;
}
