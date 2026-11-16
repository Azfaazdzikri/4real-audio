cmake_minimum_required(VERSION 3.22)

project(FourRealAudio VERSION 0.9.0 LANGUAGES C CXX)

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

# --- Licensing ------------------------------------------------------------
# Every build enforces the per-guitar licences: a guitar without a valid licence passes the sound through untouched (there is no
# unlocked build). The values below are PUBLIC (server URL, the Supabase publishable "anon" key and the Ed25519 PUBLIC key), so
# they are safe to keep here; the signing (private) key lives only in the Supabase secret LICENSE_SIGNING_KEY (see server/README.md).
# They can be overridden with -D..., for example to point a test build at another server.
set(FOURREAL_LICENSE_API_URL "https://kzyyqshqaripdlderkoa.supabase.co" CACHE STRING "Licence server URL")
set(FOURREAL_LICENSE_ANON_KEY
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Imt6eXlxc2hxYXJpcGRsZGVya29hIiwicm9sZSI6ImFub24iLCJpYXQiOjE3OTQwMjA4NTksImV4cCI6MjEwOTU5Njg1OX0.foH4KJKuR6NxBxd8HMda3nL8A6fY8PTD7ao-bI0-ZLQ"
    CACHE STRING "Supabase publishable (anon) key")
set(FOURREAL_LICENSE_PUBLIC_KEY "ARjJdtOGzxIV5VV5ycCdNpVBBfftAKgmG-7WgJov9CE" CACHE STRING "Ed25519 public key, base64 (32 bytes)")
set(FOURREAL_LICENSE_ACCOUNT_URL "https://3ffect.my.id" CACHE STRING "Account site: sign-in approval, create account, forgot password")
if(NOT FOURREAL_LICENSE_API_URL OR NOT FOURREAL_LICENSE_PUBLIC_KEY OR NOT FOURREAL_LICENSE_ANON_KEY)
    message(FATAL_ERROR "FOURREAL_LICENSE_API_URL, FOURREAL_LICENSE_ANON_KEY and FOURREAL_LICENSE_PUBLIC_KEY must not be empty")
endif()
set(FOURREAL_LICENSE_DEFINES
    FOURREAL_LICENSE_API_URL="${FOURREAL_LICENSE_API_URL}"
    FOURREAL_LICENSE_ANON_KEY="${FOURREAL_LICENSE_ANON_KEY}"
    FOURREAL_LICENSE_PUBLIC_KEY="${FOURREAL_LICENSE_PUBLIC_KEY}"
    FOURREAL_LICENSE_ACCOUNT_URL="${FOURREAL_LICENSE_ACCOUNT_URL}")

# --- The plugin ---------------------------------------------------------
# ONE plugin / standalone that holds the whole guitar library (Source/Library.h): the guitar it sounds like is chosen
# inside the plugin. Formats: VST3 and Standalone (plus Audio Unit on macOS).
# VST2 is intentionally not included: Steinberg no longer licenses the VST2 SDK.
#
# The library: one trained model per guitar, models/<CODE>/model.spm. To add a guitar, add its code here and in
# Source/dsp/GuitarModels.cpp, Source/Library.cpp and Source/ui/GuitarScene.cpp.
set(FOURREAL_GUITARS LP 335 SG PRS RIC STR TCR FLC)

# Embed every model. The files are copied under unique names first (they are all called model.spm), which also makes
# CMake configure again whenever a model changes.
set(FOURREAL_SPM_COPIES "")
foreach(guitar IN LISTS FOURREAL_GUITARS)
    set(copy "${CMAKE_BINARY_DIR}/spm/guitar_${guitar}.spm")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/models/${guitar}/model.spm" "${copy}" COPYONLY)
    list(APPEND FOURREAL_SPM_COPIES "${copy}")
endforeach()

# Embedded as SpmData::guitar_<CODE>_spm (see Source/Library.cpp).
juce_add_binary_data(FourReal_Data
    HEADER_NAME SpmData.h
    NAMESPACE   SpmData
    SOURCES     ${FOURREAL_SPM_COPIES})

# The guitar photos (Source/ui/photos/<CODE>.jpg, prepared by tools/prepare_guitar_photos.py), embedded as PhotoData.
# and the pictures rendered from them by tools/render_art.cpp (Source/ui/art: one picture per selector position, a knob filmstrip).
file(GLOB FOURREAL_PHOTOS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/photos/*.jpg"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/art/*.jpg"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/ui/art/*.png")
juce_add_binary_data(FourReal_Photos
    HEADER_NAME PhotoData.h
    NAMESPACE   PhotoData
    SOURCES     ${FOURREAL_PHOTOS})

set(FOURREAL_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Library.cpp
    Source/Library.h
    Source/License.h
    Source/license/LicenseConfig.h
    Source/license/LicenseClient.cpp
    Source/license/LicenseClient.h
    Source/license/Ed25519.cpp
    Source/license/Ed25519.h
    Source/license/Token.cpp
    Source/license/Token.h
    Source/ui/AccountPanel.cpp
    Source/ui/AccountPanel.h
    Source/ui/GuitarScene.cpp
    Source/ui/GuitarScene.h
    Source/ui/Photos.cpp
    Source/ui/Photos.h
    Source/ui/Look.h
    Source/ui/GuitarView.cpp
    Source/ui/GuitarView.h
    Source/ui/LibraryBrowser.cpp
    Source/ui/LibraryBrowser.h
    Source/dsp/GuitarCircuit.cpp
    Source/dsp/GuitarCircuit.h
    Source/dsp/GuitarModels.cpp
    Source/dsp/GuitarModels.h
    Source/dsp/FilterDesign.cpp
    Source/dsp/FilterDesign.h
    Source/dsp/CrossfadeFir.h
    Source/dsp/MiniJson.h
    Source/dsp/SpecModel.cpp
    Source/dsp/SpecModel.h
    Source/dsp/SpecEngine.cpp
    Source/dsp/SpecEngine.h)

# Windows: VST3 + Standalone. macOS: VST3 + Audio Unit + Standalone (Studio One, Logic, GarageBand, Ableton, Reaper ...).
set(FOURREAL_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND FOURREAL_FORMATS AU)
endif()

juce_add_plugin(FourReal
    COMPANY_NAME                "4Real"
    PLUGIN_MANUFACTURER_CODE    Frea
    PLUGIN_CODE                 FrLb
    FORMATS                     ${FOURREAL_FORMATS}
    PRODUCT_NAME                "4real-audio"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx)

target_sources(FourReal PRIVATE ${FOURREAL_SOURCES})

target_compile_definitions(FourReal PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    ${FOURREAL_LICENSE_DEFINES})

target_link_libraries(FourReal
    PRIVATE
        juce::juce_audio_utils
        FourReal_Data
        FourReal_Photos
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Optional: headless editor preview (renders the real editor to PNG files, no plugin host or sound card) -------------
# cmake -S . -B build-preview -DFOURREAL_BUILD_PREVIEW=ON && cmake --build build-preview --target FourReal_UiPreview
option(FOURREAL_BUILD_PREVIEW "Build the headless editor preview tool" OFF)
if(FOURREAL_BUILD_PREVIEW)
    juce_add_console_app(FourReal_UiPreview PRODUCT_NAME "FourRealUiPreview")
    target_sources(FourReal_UiPreview PRIVATE ${FOURREAL_SOURCES} tools/ui_preview.cpp)
    target_compile_definitions(FourReal_UiPreview PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_ALSA=0
        JUCE_JACK=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STANDALONE_APPLICATION=1
        ${FOURREAL_LICENSE_DEFINES})
    target_link_libraries(FourReal_UiPreview
        PRIVATE
            juce::juce_gui_basics
            juce::juce_audio_processors
            FourReal_Data
            FourReal_Photos
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# --- Optional DSP tests (no JUCE needed) -------------------------------------
option(FOURREAL_BUILD_TESTS "Build the standalone DSP tests" OFF)
if(FOURREAL_BUILD_TESTS)
    add_executable(dsp_test
        tests/dsp_test.cpp
        Source/dsp/GuitarCircuit.cpp
        Source/dsp/GuitarModels.cpp
        Source/dsp/FilterDesign.cpp)
    target_include_directories(dsp_test PRIVATE Source/dsp)

    add_executable(spm_test
        tests/spm_test.cpp
        Source/dsp/SpecModel.cpp
        Source/dsp/SpecEngine.cpp)
    target_include_directories(spm_test PRIVATE Source/dsp)

    add_executable(license_test
        tests/license_test.cpp
        Source/license/Ed25519.cpp
        Source/license/Token.cpp)
    target_include_directories(license_test PRIVATE Source)

    add_executable(library_test
        tests/library_test.cpp
        Source/dsp/GuitarModels.cpp
        Source/dsp/GuitarCircuit.cpp
        Source/dsp/FilterDesign.cpp
        Source/dsp/SpecModel.cpp)
    target_include_directories(library_test PRIVATE Source/dsp)
endif()
