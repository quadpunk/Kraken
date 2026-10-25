cmake_minimum_required(VERSION 3.22)
project(KRAKEN VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Kraken
    COMPANY_NAME "kraken"
    BUNDLE_ID com.kraken.kratken
    PLUGIN_MANUFACTURER_CODE Krkn
    PLUGIN_CODE Kr4k
    FORMATS VST3 Standalone
    PRODUCT_NAME "kr@ken"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

target_sources(Kraken PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Analyzer.cpp
    Source/AnalyzerCore.cpp
    Source/PresetManager.cpp)

target_compile_definitions(Kraken PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Kraken
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
