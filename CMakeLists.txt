cmake_minimum_required(VERSION 3.22)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS architectures")
endif()
project(Transcription VERSION 1.0.4 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRANSCRIPTION_BUILD_PLUGIN "Build the JUCE instrument" ON)
option(TRANSCRIPTION_BUILD_TESTS "Build offline regression tests" ON)
option(TRANSCRIPTION_COPY_PLUGIN_AFTER_BUILD "Install plugins after building" OFF)

add_library(TranscriptionDSP STATIC
    Source/Model/Genome.cpp Source/Presets.cpp Source/Dsp/TranscriptionEngine.cpp
    Source/Dsp/SubtractiveCore.cpp Source/Dsp/FxChain.cpp)
target_include_directories(TranscriptionDSP PUBLIC Source)
set_target_properties(TranscriptionDSP PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(TranscriptionDSP PRIVATE /W4 /utf-8)
else()
    target_compile_options(TranscriptionDSP PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TRANSCRIPTION_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
    set(transcription_formats VST3 Standalone)
    if(APPLE)
        list(APPEND transcription_formats AU)
    endif()
    juce_add_plugin(Transcription
        PRODUCT_NAME "ChemE-Transcription"
        COMPANY_NAME "iisaacbeats.cn"
        BUNDLE_ID "cn.iisaacbeats.ChemETranscription"
        PLUGIN_MANUFACTURER_CODE Isac
        PLUGIN_CODE CE03
        FORMATS ${transcription_formats}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        NEEDS_WEB_BROWSER FALSE
        NEEDS_CURL FALSE
        VST3_CATEGORIES Instrument Synth
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        COPY_PLUGIN_AFTER_BUILD ${TRANSCRIPTION_COPY_PLUGIN_AFTER_BUILD})
    juce_generate_juce_header(Transcription)
    target_sources(Transcription PRIVATE
        Source/Parameters.cpp Source/Parameters.h
        Source/PluginProcessor.cpp Source/PluginProcessor.h
        Source/PluginEditor.cpp Source/PluginEditor.h
        Source/UI/GeneCanvas.cpp Source/UI/GeneCanvas.h
        Source/UI/ScientificLookAndFeel.h)
    target_compile_definitions(Transcription PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(Transcription PRIVATE TranscriptionDSP juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(MSVC)
        target_compile_options(Transcription PRIVATE /utf-8)
    endif()
endif()

if(TRANSCRIPTION_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(TranscriptionDSPTests Tests/DspTests.cpp)
    target_link_libraries(TranscriptionDSPTests PRIVATE TranscriptionDSP Threads::Threads)
    add_test(NAME TranscriptionDSPTests COMMAND TranscriptionDSPTests)
    if(TRANSCRIPTION_BUILD_PLUGIN)
        juce_add_console_app(TranscriptionStateTests PRODUCT_NAME "Transcription State Tests")
        target_sources(TranscriptionStateTests PRIVATE Tests/StateTests.cpp)
        target_include_directories(TranscriptionStateTests PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/Transcription_artefacts/JuceLibraryCode")
        target_link_libraries(TranscriptionStateTests PRIVATE
            Transcription TranscriptionDSP juce::juce_audio_utils)
        if(MSVC)
            target_compile_options(TranscriptionStateTests PRIVATE /utf-8)
        endif()
        add_test(NAME TranscriptionStateTests COMMAND TranscriptionStateTests)
    endif()
endif()
