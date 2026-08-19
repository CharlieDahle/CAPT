cmake_minimum_required(VERSION 3.22)
project(CAPT VERSION 0.1.0)

add_subdirectory(JUCE)

juce_add_gui_app(CAPT
    PRODUCT_NAME "CAPT")

target_sources(CAPT
    PRIVATE
        Source/Main.cpp)

target_compile_definitions(CAPT
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(CAPT
    PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)