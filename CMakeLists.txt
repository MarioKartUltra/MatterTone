cmake_minimum_required(VERSION 3.22)

project(MatterTone VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(MATTER_USE_FOLEYS "Build the GUI with foleys_gui_magic (WYSIWYG editor + meters)" OFF)
option(MATTER_USE_FCWT   "Build the real-time fCWT scalogram view (needs FFTW3f)"        OFF)

# -----------------------------------------------------------------------------
# JUCE — use a local checkout (no network / no global install).
#   Priority: -DJUCE_DIR=...  >  ./external/JUCE  >  ~/Downloads/JUCE  >  FetchContent
# -----------------------------------------------------------------------------
if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(STATUS "MatterTone: using JUCE at ${JUCE_DIR}")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(STATUS "MatterTone: using JUCE at external/JUCE")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
elseif(EXISTS "$ENV{HOME}/Downloads/JUCE/CMakeLists.txt")
    message(STATUS "MatterTone: using JUCE at $ENV{HOME}/Downloads/JUCE")
    add_subdirectory("$ENV{HOME}/Downloads/JUCE" "${CMAKE_BINARY_DIR}/JUCE")
else()
    message(STATUS "MatterTone: no local JUCE found — fetching JUCE 8.0.7")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.7)
    FetchContent_MakeAvailable(JUCE)
endif()

# =============================================================================
#  Plugin target
# =============================================================================
juce_add_plugin(MatterTone
    PLUGIN_MANUFACTURER_CODE Mttn
    PLUGIN_CODE              Mtpc
    COMPANY_NAME             "MatterTone"
    BUNDLE_ID                com.mattertone.poc
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  TRUE
    PLUGIN_NAME              "MatterTone POC"
    PRODUCT_NAME             "MatterTone"
    FORMATS                  VST3 Standalone AU)

juce_generate_juce_header(MatterTone)

target_sources(MatterTone PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/dsp/MatterVoice.cpp)

target_include_directories(MatterTone PRIVATE Source Source/dsp)

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
    # Locate a local foleys checkout: -DFOLEYS_DIR=... > external/foleys_gui_magic
    if(DEFINED FOLEYS_DIR AND EXISTS "${FOLEYS_DIR}/modules/foleys_gui_magic/foleys_gui_magic.h")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/foleys_gui_magic/modules/foleys_gui_magic/foleys_gui_magic.h")
        set(FOLEYS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/foleys_gui_magic")
    else()
        message(FATAL_ERROR "MATTER_USE_FOLEYS=ON but no foleys_gui_magic checkout found (set -DFOLEYS_DIR=...)")
    endif()
    message(STATUS "MatterTone: using foleys_gui_magic at ${FOLEYS_DIR}")

    # Consume ONLY the JUCE module (juce_add_module) so foleys' own examples/tests
    # CMake — which FetchContents a second JUCE — never runs.
    juce_add_module("${FOLEYS_DIR}/modules/foleys_gui_magic")
    target_link_libraries(MatterTone PRIVATE foleys_gui_magic)

    # foleys 1.3.x uses C++20; the palette (WYSIWYG editor) is on; no OpenGL dep.
    set_target_properties(MatterTone PROPERTIES CXX_STANDARD 20)
    target_compile_definitions(MatterTone PUBLIC
        MATTER_USE_FOLEYS=1
        FOLEYS_SHOW_GUI_EDITOR_PALLETTE=1
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
    # Locate a local fCWT checkout (no network): -DFCWT_DIR=... > external/fCWT > ~/projects/fCWT
    if(DEFINED FCWT_DIR AND EXISTS "${FCWT_DIR}/src/fcwt/fcwt.cpp")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/fCWT/src/fcwt/fcwt.cpp")
        set(FCWT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/fCWT")
    elseif(EXISTS "$ENV{HOME}/projects/fCWT/src/fcwt/fcwt.cpp")
        set(FCWT_DIR "$ENV{HOME}/projects/fCWT")
    else()
        message(FATAL_ERROR "MATTER_USE_FCWT=ON but no fCWT checkout found (set -DFCWT_DIR=...)")
    endif()
    message(STATUS "MatterTone: using fCWT at ${FCWT_DIR}")

    # Prefer the STATIC fftw archive so the plugin has no external dylib dependency
    # (self-contained; and a host's plugin scanner won't reject a missing/foreign
    # /opt/homebrew dylib). Fall back to the shared lib if the archive is absent.
    set(_matter_saved_suffixes ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    find_library(FFTW3F_STATIC fftw3f PATHS /opt/homebrew/opt/fftw/lib /usr/local/lib)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_matter_saved_suffixes})
    if(FFTW3F_STATIC)
        set(FFTW3F_LIB ${FFTW3F_STATIC})
        message(STATUS "MatterTone: linking FFTW statically (${FFTW3F_LIB})")
    else()
        find_library(FFTW3F_LIB fftw3f PATHS /opt/homebrew/opt/fftw/lib /usr/local/lib REQUIRED)
        message(WARNING "MatterTone: static fftw not found — linking shared libfftw3f (external dylib dependency)")
    endif()
    find_path(FFTW3_INC fftw3.h PATHS /opt/homebrew/opt/fftw/include /usr/local/include REQUIRED)

    # fCWT compiled scalar (no -mavx on arm64) and single-threaded (no OpenMP dep).
    target_compile_definitions(MatterTone PRIVATE MATTER_USE_FCWT=1 SINGLE_THREAD=1)
    target_include_directories(MatterTone PRIVATE "${FFTW3_INC}" "${FCWT_DIR}/src/fcwt")
    target_sources(MatterTone PRIVATE
        "${FCWT_DIR}/src/fcwt/fcwt.cpp"
        Source/ui/ScalogramEngine.cpp)
    target_link_libraries(MatterTone PRIVATE ${FFTW3F_LIB})

    # fcwt.cpp is third-party: silence its warnings so JUCE's warning flags don't error.
    set_source_files_properties("${FCWT_DIR}/src/fcwt/fcwt.cpp"
        PROPERTIES COMPILE_FLAGS "-w")
endif()

# =============================================================================
#  Offline render + test harness (pure C++, NO JUCE) — always built
# =============================================================================
add_executable(matter_render tools/matter_render.cpp Source/dsp/MatterVoice.cpp)
target_include_directories(matter_render PRIVATE Source/dsp)
target_compile_definitions(matter_render PRIVATE MATTER_ENABLE_MONITOR=1)
set_target_properties(matter_render PROPERTIES CXX_STANDARD 17)

enable_testing()
add_test(NAME dsp_core_tests
         COMMAND matter_render --test-only --out ${CMAKE_BINARY_DIR}/renders)
