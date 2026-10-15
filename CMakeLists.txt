# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 Abraham Joseph Okayli Masaryk
cmake_minimum_required(VERSION 3.22)

project(MatterTone VERSION 2.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(MATTER_USE_FOLEYS "Build the GUI with foleys_gui_magic (WYSIWYG editor + meters)" ON)
# The foleys WYSIWYG editor is a second window that opens next to the plugin.
# OFF by default. Configure with -DMATTERTONE_GUI_EDITOR=ON to design the layout
# (it also bakes this source path into the binary, so never ship that build).
option(MATTERTONE_GUI_EDITOR "Open the foleys WYSIWYG editor window with the plugin" OFF)
# OFF builds the bundles without copying them into ~/Library/Audio/Plug-Ins.
option(MATTERTONE_INSTALL_AFTER_BUILD "Copy the plugins into the user plug-in folders after building" ON)
option(MATTER_USE_FCWT   "Build the real-time fCWT scalogram view (needs FFTW3f)"        ON)

# JUCE and foleys_gui_magic: a local checkout, else fetched (cmake/ThirdParty.cmake)
set(THIRDPARTY_WITH_FOLEYS ${MATTER_USE_FOLEYS})
include(cmake/ThirdParty.cmake)

# =============================================================================
#  Plugin target
# =============================================================================
# v2 identity (V2_CONVENTIONS.md): NEW plugin code / product name / bundle id so
# the installed v1 bundles ("MatterTone", Mtpc, com.mattertone.poc) coexist.
juce_add_plugin(MatterTone
    PLUGIN_MANUFACTURER_CODE Mttn
    PLUGIN_CODE              Mtp2
    COMPANY_NAME             "MatterTone"
    BUNDLE_ID                com.mattertone.v2
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  ${MATTERTONE_INSTALL_AFTER_BUILD}
    PLUGIN_NAME              "MatterTone II"
    PRODUCT_NAME             "MatterTone II"
    FORMATS                  VST3 Standalone AU)

juce_generate_juce_header(MatterTone)

# Default GUI layout (foleys magic.xml) baked in as binary data.
juce_add_binary_data(MatterToneAssets SOURCES Resources/magic.xml)

target_sources(MatterTone PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/dsp/MatterVoice.cpp)

target_include_directories(MatterTone PRIVATE Source Source/dsp Source/common)
target_link_libraries(MatterTone PRIVATE MatterToneAssets)

target_compile_definitions(MatterTone PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(MatterTone PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- optional foleys_gui_magic (WYSIWYG GUI + visualisers) ------------------
if(MATTER_USE_FOLEYS)
    target_link_libraries(MatterTone PRIVATE foleys_gui_magic)

    # foleys 1.3.x uses C++20; the palette (WYSIWYG editor) is on; no OpenGL dep.
    set_target_properties(MatterTone PROPERTIES CXX_STANDARD 20)
    target_compile_definitions(MatterTone PUBLIC
        MATTER_USE_FOLEYS=1
        FOLEYS_SHOW_GUI_EDITOR_PALLETTE=$<BOOL:${MATTERTONE_GUI_EDITOR}>
        FOLEYS_ENABLE_BINARY_DATA=0
        FOLEYS_ENABLE_OPEN_GL_CONTEXT=0)

    # When the scalogram is also enabled, unify the FFT engine: route juce::dsp::FFT
    # (used by foleys' analyser) through the SAME FFTW as fCWT.
    if(MATTER_USE_FCWT)
        target_compile_definitions(MatterTone PUBLIC JUCE_DSP_USE_SHARED_FFTW=1)
    endif()
endif()

# ---- optional fCWT real-time scalogram (FFTW3f) ----------------------------
if(MATTER_USE_FCWT)
    # fCWT is vendored under Source/third_party/fcwt (Apache-2.0); FFTW3f per cmake/FFTW3f.cmake.
    include(cmake/FFTW3f.cmake)

    # fCWT compiled scalar (no -mavx on arm64) and single-threaded (no OpenMP dep).
    target_compile_definitions(MatterTone PRIVATE MATTER_USE_FCWT=1 SINGLE_THREAD=1)
    target_include_directories(MatterTone PRIVATE "${FFTW3F_INC}" Source/third_party/fcwt)
    target_sources(MatterTone PRIVATE
        Source/third_party/fcwt/fcwt.cpp
        Source/ui/ScalogramEngine.cpp)
    target_link_libraries(MatterTone PRIVATE ${FFTW3F_LIB})

    # fcwt.cpp is third-party: silence its warnings so JUCE's warning flags don't error.
    set_source_files_properties(Source/third_party/fcwt/fcwt.cpp
        PROPERTIES COMPILE_FLAGS "-w")
endif()

# =============================================================================
#  Offline render + test harness (pure C++, NO JUCE) — always built
# =============================================================================
add_executable(matter_render tools/matter_render.cpp Source/dsp/MatterVoice.cpp)
target_include_directories(matter_render PRIVATE Source/dsp Source/common)
target_compile_definitions(matter_render PRIVATE MATTER_ENABLE_MONITOR=1)
set_target_properties(matter_render PROPERTIES CXX_STANDARD 17)

enable_testing()
add_test(NAME dsp_core_tests
         COMMAND matter_render --test-only --out ${CMAKE_BINARY_DIR}/renders)

# =============================================================================
#  Offscreen UI screenshot harness (dev tool — NOT a test, NOT a bundle)
#  Links the juce_add_plugin shared-code static library so it gets the
#  processor + foleys editor + all JUCE modules + BinaryData, without building
#  or installing any VST3/AU/Standalone bundle.
# =============================================================================
if(MATTER_USE_FOLEYS)
    add_executable(mattertone_shot tools/mattertone_shot.cpp)
    # Re-export the shared code's resolved usage requirements (module headers,
    # JUCE_MODULE_AVAILABLE_*, JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED, JucePlugin_*).
    target_include_directories(mattertone_shot PRIVATE $<TARGET_PROPERTY:MatterTone,INCLUDE_DIRECTORIES>)
    target_compile_definitions(mattertone_shot PRIVATE $<TARGET_PROPERTY:MatterTone,COMPILE_DEFINITIONS>)
    target_compile_options(mattertone_shot PRIVATE $<TARGET_PROPERTY:MatterTone,COMPILE_OPTIONS>)
    target_link_libraries(mattertone_shot PRIVATE MatterTone)
    set_target_properties(mattertone_shot PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
endif()
