cmake_minimum_required(VERSION 3.22)

project(KlangKunstWorldWireframe VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(/Users/md/JUCE JUCE)

juce_add_gui_app(KlangKunstWorldWireframe
    PRODUCT_NAME "KlangKunstWorldWireframe"
)

juce_generate_juce_header(KlangKunstWorldWireframe)

target_sources(KlangKunstWorldWireframe
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.h
        Source/MainComponent.cpp
)

target_compile_definitions(KlangKunstWorldWireframe
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(KlangKunstWorldWireframe
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
