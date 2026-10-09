cmake_minimum_required(VERSION 3.22)

project(FourRealAudio VERSION 0.1.0 LANGUAGES C CXX)

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

# --- Plugin -------------------------------------------------------------
# Formats: VST3 (loaded by Studio One) and Standalone.
# VST2 is intentionally not included: Steinberg no longer licenses the VST2 SDK.
juce_add_plugin(FourRealAudio
    COMPANY_NAME                "4Real"
    PLUGIN_MANUFACTURER_CODE    Frea
    PLUGIN_CODE                 Fra1
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "4real-audio"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx)

target_sources(FourRealAudio PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_compile_definitions(FourRealAudio PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FourRealAudio
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
