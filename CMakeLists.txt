cmake_minimum_required(VERSION 3.22)

project(FourRealAudio VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE ---------------------------------------------------------------
# Default: download JUCE 9.0.3 automatically.
# Alternative: point to a local JUCE checkout with -DFOURREAL_JUCE_DIR=C:/path/to/JUCE
set(FOURREAL_JUCE_DIR "" CACHE PATH "Optional path to a local JUCE checkout")

if(FOURREAL_JUCE_DIR)
    add_subdirectory("${FOURREAL_JUCE_DIR}" juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.3
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- Plugins ------------------------------------------------------------
# One plugin per target guitar, all built from the same sources. The variant is selected by the
# compile definition FOURREAL_MODEL_<CODE> (see Source/PluginProcessor.cpp).
# Formats: VST3 (loaded by Studio One) and Standalone.
# VST2 is intentionally not included: Steinberg no longer licenses the VST2 SDK.
#
# Build only some of them with:  -DFOURREAL_MODELS="LP;STR"
set(FOURREAL_ALL_MODELS "LP;335;SG;PRS;RIC;STR;TCR;FLC")
set(FOURREAL_MODELS "${FOURREAL_ALL_MODELS}" CACHE STRING "Plugin variants to build (semicolon separated)")

# Unique 4-character VST3 plugin code per variant.
set(FOURREAL_PLUGIN_CODE_LP  FrLP)
set(FOURREAL_PLUGIN_CODE_335 Fr35)
set(FOURREAL_PLUGIN_CODE_SG  FrSG)
set(FOURREAL_PLUGIN_CODE_PRS FrPR)
set(FOURREAL_PLUGIN_CODE_RIC FrRC)
set(FOURREAL_PLUGIN_CODE_STR FrST)
set(FOURREAL_PLUGIN_CODE_TCR FrTC)
set(FOURREAL_PLUGIN_CODE_FLC FrFL)

set(FOURREAL_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/dsp/GuitarCircuit.cpp
    Source/dsp/GuitarCircuit.h
    Source/dsp/GuitarModels.cpp
    Source/dsp/GuitarModels.h
    Source/dsp/FilterDesign.cpp
    Source/dsp/FilterDesign.h
    Source/dsp/CrossfadeFir.h)

function(fourreal_add_plugin MODEL)
    set(target FourReal_${MODEL})
    juce_add_plugin(${target}
        COMPANY_NAME                "4Real"
        PLUGIN_MANUFACTURER_CODE    Frea
        PLUGIN_CODE                 ${FOURREAL_PLUGIN_CODE_${MODEL}}
        FORMATS                     VST3 Standalone
        PRODUCT_NAME                "4real-audio ${MODEL}"
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        VST3_CATEGORIES             Fx)

    target_sources(${target} PRIVATE ${FOURREAL_SOURCES})

    target_compile_definitions(${target} PUBLIC
        FOURREAL_MODEL_${MODEL}=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

foreach(model IN LISTS FOURREAL_MODELS)
    if(NOT DEFINED FOURREAL_PLUGIN_CODE_${model})
        message(FATAL_ERROR "Unknown model '${model}'. Valid: ${FOURREAL_ALL_MODELS}")
    endif()
    fourreal_add_plugin(${model})
endforeach()

# --- Optional DSP tests (no JUCE needed) -------------------------------------
option(FOURREAL_BUILD_TESTS "Build the standalone DSP tests" OFF)
if(FOURREAL_BUILD_TESTS)
    add_executable(dsp_test
        tests/dsp_test.cpp
        Source/dsp/GuitarCircuit.cpp
        Source/dsp/GuitarModels.cpp
        Source/dsp/FilterDesign.cpp)
    target_include_directories(dsp_test PRIVATE Source/dsp)
endif()
