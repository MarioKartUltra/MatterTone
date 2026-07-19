// =============================================================================
//  MatterTone POC — Presets.h
//  The ten factory presets, expressed purely as MatterParams (parameter STATES,
//  never audio files). Shared by the JUCE plugin and the offline render harness
//  so "what a preset sounds like" is identical in both.  Pure C++17.
// =============================================================================
#pragma once

#include <array>
#include "MatterParams.h"

namespace matter
{
struct Preset { const char* name; MatterParams params; };

inline MatterParams presetRubberTwang()
{
    MatterParams p;                     // soft, long, pitch-bending twang
    p.output = 0.85;
    p.tension = 0.9;   p.nonlinStiffness = 0.7; p.hardening = -0.6; // softening -> rubbery
    p.damping = 0.10;  p.nonlinDamping = 0.25;  p.coupling = 0.3;
    p.brittleness = 0.15; p.adhesion = 0.2; p.roughness = 0.2;
    p.contactHardness = 0.1; p.contactLoss = 0.2; p.crackleCoupling = 0.1;
    p.excitation = ExcitationMode::Pluck; p.pluckEnergy = 0.8; p.strikePosition = 0.25;
    p.modeRatio = 1.5; p.eventMix = 0.25; p.memory = 0.3; p.healing = 0.3;
    return p;
}

inline MatterParams presetTightElasticSnap()
{
    MatterParams p;                     // short, snappy elastic release
    p.output = 1.7;
    p.tension = 1.3; p.nonlinStiffness = 0.5; p.hardening = 0.3;
    p.damping = 0.3; p.nonlinDamping = 0.2; p.coupling = 0.2;
    p.brittleness = 0.8; p.adhesion = 0.5; p.roughness = 0.2;
    p.contactHardness = 0.2; p.contactLoss = 0.3; p.crackleCoupling = 0.15;
    p.excitation = ExcitationMode::LoadSnap; p.loadingRate = 0.8; p.snapDepth = 0.6;
    p.eventMix = 0.6; p.pluckEnergy = 0.4; p.memory = 0.2;
    return p;
}

inline MatterParams presetPlasticPop()
{
    MatterParams p;                     // hard, dry, single contact pop
    p.output = 0.9;
    p.tension = 1.6; p.nonlinStiffness = 0.4; p.hardening = 0.5;
    p.damping = 0.5; p.nonlinDamping = 0.3; p.coupling = 0.15;
    p.brittleness = 0.7; p.adhesion = 0.25; p.roughness = 0.3;
    p.contactHardness = 0.8; p.contactLoss = 0.5; p.crackleCoupling = 0.1;
    p.excitation = ExcitationMode::Pluck; p.pluckEnergy = 0.7; p.strikePosition = 0.1;
    p.eventMix = 0.7; p.snapDepth = 0.5; p.memory = 0.15;
    return p;
}

inline MatterParams presetDryCrackle()
{
    MatterParams p;                     // isolated pops: low network coupling, rough
    p.output = 1.5;
    p.tension = 1.1; p.nonlinStiffness = 0.4; p.hardening = 0.2;
    p.damping = 0.35; p.nonlinDamping = 0.2; p.coupling = 0.2;
    p.brittleness = 0.6; p.adhesion = 0.6; p.roughness = 0.85;
    p.contactHardness = 0.3; p.contactLoss = 0.3; p.crackleCoupling = 0.08; // isolated
    p.excitation = ExcitationMode::LoadSnap; p.loadingRate = 0.45; p.snapDepth = 0.55;
    p.eventMix = 0.8; p.memory = 0.4; p.healing = 0.25;
    return p;
}

inline MatterParams presetAvalancheCrackle()
{
    MatterParams p;                     // cascades: strong network coupling
    p.output = 1.35;
    p.tension = 1.0; p.nonlinStiffness = 0.45; p.hardening = 0.2;
    p.damping = 0.3; p.nonlinDamping = 0.2; p.coupling = 0.25;
    p.brittleness = 0.5; p.adhesion = 0.7; p.roughness = 0.8;
    p.contactHardness = 0.3; p.contactLoss = 0.25; p.crackleCoupling = 0.85; // cascades
    p.excitation = ExcitationMode::LoadSnap; p.loadingRate = 0.6; p.snapDepth = 0.5;
    p.eventMix = 0.85; p.memory = 0.5; p.healing = 0.2;
    return p;
}

inline MatterParams presetCreakingJoint()
{
    MatterParams p;                     // slow stick-slip chatter
    p.output = 1.5;
    p.tension = 0.7; p.nonlinStiffness = 0.5; p.hardening = -0.2;
    p.damping = 0.25; p.nonlinDamping = 0.35; p.coupling = 0.3;
    p.brittleness = 0.45; p.adhesion = 0.8; p.roughness = 0.7;
    p.contactHardness = 0.5; p.contactLoss = 0.4; p.crackleCoupling = 0.5;
    p.excitation = ExcitationMode::LoadSnap; p.loadingRate = 0.2; p.snapDepth = 0.55;
    p.eventMix = 0.75; p.memory = 0.7; p.healing = 0.15; p.rootNote = 48;
    return p;
}

inline MatterParams presetStableNonlinearBell()
{
    MatterParams p;                     // pitched inharmonic metallic tone
    p.output = 1.9;
    p.tension = 1.0; p.nonlinStiffness = 0.35; p.hardening = 0.4;
    p.damping = 0.06; p.nonlinDamping = 0.05; p.coupling = 0.4;
    p.brittleness = 0.2; p.adhesion = 0.1; p.roughness = 0.15;
    p.contactHardness = 0.05; p.contactLoss = 0.1; p.crackleCoupling = 0.05;
    p.excitation = ExcitationMode::Pluck; p.pluckEnergy = 0.55; p.strikePosition = 0.4;
    p.modeRatio = 2.756; // inharmonic (bell-like)
    p.eventMix = 0.2; p.memory = 0.1;
    return p;
}

inline MatterParams presetSubharmonicDrone()
{
    MatterParams p;                     // strong coupling -> period doubling / mode lock
    p.output = 0.95;
    p.tension = 0.8; p.nonlinStiffness = 0.85; p.hardening = 0.7;
    p.damping = 0.05; p.nonlinDamping = 0.08; p.coupling = 0.85;
    p.brittleness = 0.3; p.adhesion = 0.3; p.roughness = 0.2;
    p.contactHardness = 0.15; p.contactLoss = 0.15; p.crackleCoupling = 0.1;
    p.excitation = ExcitationMode::Pluck; p.pluckEnergy = 0.95; p.strikePosition = 0.5;
    p.modeRatio = 2.02; // near 2:1 -> subharmonic locking
    p.eventMix = 0.3; p.memory = 0.2; p.rootNote = 41;
    return p;
}

inline MatterParams presetModeLockedScale()
{
    MatterParams p;                     // just-intonation ratio set, pitched
    p.output = 1.5;
    p.tension = 1.0; p.nonlinStiffness = 0.45; p.hardening = 0.35;
    p.damping = 0.09; p.nonlinDamping = 0.08; p.coupling = 0.5;
    p.brittleness = 0.25; p.adhesion = 0.15; p.roughness = 0.15;
    p.contactHardness = 0.05; p.contactLoss = 0.1; p.crackleCoupling = 0.05;
    p.excitation = ExcitationMode::Pluck; p.pluckEnergy = 0.6; p.strikePosition = 0.35;
    p.tuningMode = TuningMode::RatioSet; p.attractorSet = 0; p.rootNote = 57;
    p.modeRatio = 1.5; p.eventMix = 0.2;
    return p;
}

inline MatterParams presetUnfamiliarAttractorScale()
{
    MatterParams p;                     // attractor-parameterised inharmonic scale
    p.output = 1.25;
    p.tension = 1.0; p.nonlinStiffness = 0.6; p.hardening = 0.5;
    p.damping = 0.09; p.nonlinDamping = 0.1; p.coupling = 0.55;
    p.brittleness = 0.35; p.adhesion = 0.25; p.roughness = 0.25;
    p.contactHardness = 0.1; p.contactLoss = 0.12; p.crackleCoupling = 0.1;
    p.excitation = ExcitationMode::Pluck; p.pluckEnergy = 0.65; p.strikePosition = 0.4;
    p.tuningMode = TuningMode::AttractorScale; p.attractorSet = 1; p.rootNote = 55;
    p.scaleSpread = 1.0; p.stabilityBias = 0.5; p.energyToPitch = 0.4;
    p.eventMix = 0.35;
    return p;
}

// =============================================================================
//  v2 percussion presets (PercussionEngine: membrane + Berger + Helmholtz)
// =============================================================================
inline MatterParams presetMembraneKick()
{
    MatterParams p;                     // punchy pitch-glide kick (Berger thump)
    p.engine = EngineMode::Percussion;
    p.output = 1.2;
    p.tension = 0.25;                   // head tuned an octave below the key
    p.membraneModes = 6;  p.bergerAmount = 0.85;
    p.cavityAmount = 0.55; p.cavityTune = 0.4; p.cavityLoss = 0.45;
    p.wireAmount = 0.0;   p.dampTilt = 0.6;  p.malletHardness = 0.5;
    p.damping = 0.4;  p.contactLoss = 0.5;
    p.pluckEnergy = 0.85; p.strikePosition = 0.1; p.velocitySens = 0.8;
    p.attack = 0.0; p.eventMix = 0.3; p.rootNote = 36;
    return p;
}

inline MatterParams presetCavernKick()
{
    MatterParams p;                     // boomy, cavity-forward, soft beater
    p.engine = EngineMode::Percussion;
    p.output = 1.3;
    p.tension = 0.25;
    p.membraneModes = 5;  p.bergerAmount = 0.55;
    p.cavityAmount = 0.8; p.cavityTune = 0.3; p.cavityLoss = 0.25;
    p.wireAmount = 0.0;   p.dampTilt = 0.4;  p.malletHardness = 0.3;
    p.damping = 0.18; p.contactLoss = 0.35;
    p.pluckEnergy = 0.95; p.strikePosition = 0.05; p.velocitySens = 0.7;
    p.attack = 0.0; p.eventMix = 0.2; p.rootNote = 36;
    return p;
}

inline MatterParams presetWireSnare()
{
    MatterParams p;                     // bright membrane + wire contact rattle
    p.engine = EngineMode::Percussion;
    p.output = 1.1;
    p.tension = 0.9; p.oversampling = 8;    // wire collisions: 8x (aliasing)
    p.membraneModes = 18; p.bergerAmount = 0.3;
    p.cavityAmount = 0.3; p.cavityTune = 0.9; p.cavityLoss = 0.5;
    p.wireAmount = 0.75;  p.dampTilt = 0.5;  p.malletHardness = 0.75;
    p.damping = 0.35; p.contactLoss = 0.4;
    p.pluckEnergy = 0.7; p.strikePosition = 0.35; p.velocitySens = 0.85;
    p.attack = 0.0; p.eventMix = 0.7; p.rootNote = 50;
    return p;
}

inline MatterParams presetLooseRattle()
{
    MatterParams p;                     // loose wires, darker shell, brushy
    p.engine = EngineMode::Percussion;
    p.output = 1.2;
    p.tension = 0.7; p.oversampling = 8;
    p.membraneModes = 16; p.bergerAmount = 0.2;
    p.cavityAmount = 0.2; p.cavityTune = 1.1; p.cavityLoss = 0.6;
    p.wireAmount = 0.95;  p.dampTilt = 0.35; p.malletHardness = 0.45;
    p.damping = 0.5; p.contactLoss = 0.5;
    p.pluckEnergy = 0.6; p.strikePosition = 0.5; p.velocitySens = 0.8;
    p.attack = 0.0; p.eventMix = 0.85; p.rootNote = 50;
    return p;
}

inline std::array<Preset, 14> factoryPresets()
{
    return {{
        { "Rubber Twang",              presetRubberTwang() },
        { "Tight Elastic Snap",        presetTightElasticSnap() },
        { "Plastic Pop",               presetPlasticPop() },
        { "Dry Crackle",               presetDryCrackle() },
        { "Avalanche Crackle",         presetAvalancheCrackle() },
        { "Creaking Joint",            presetCreakingJoint() },
        { "Stable Nonlinear Bell",     presetStableNonlinearBell() },
        { "Subharmonic Drone",         presetSubharmonicDrone() },
        { "Mode-Locked Scale",         presetModeLockedScale() },
        { "Unfamiliar Attractor Scale",presetUnfamiliarAttractorScale() },
        { "Membrane Kick",             presetMembraneKick() },
        { "Cavern Kick",               presetCavernKick() },
        { "Wire Snare",                presetWireSnare() },
        { "Loose Rattle",              presetLooseRattle() },
    }};
}
} // namespace matter
