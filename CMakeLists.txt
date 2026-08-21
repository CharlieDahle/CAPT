cmake_minimum_required(VERSION 3.22)
project(CAPT VERSION 0.1.0 LANGUAGES C CXX OBJCXX)

add_subdirectory(JUCE)

juce_add_gui_app(CAPT
    PRODUCT_NAME "CAPT"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "CAPT needs microphone access to record audio tracks.")

target_sources(CAPT
    PRIVATE
        Source/Main.cpp
        Source/MicrophonePermission.h
        Source/MicrophonePermission.mm

        Source/Core/Types.h
        Source/Core/Tempo.h
        Source/Core/Tempo.cpp

        Source/UI/IconToggleButton.h
        Source/UI/LevelMeter.h
        Source/UI/GridPainter.h
        Source/UI/GridPainter.cpp
        Source/UI/TimelineRuler.h
        Source/UI/TimelineRuler.cpp
        Source/UI/PianoRoll.h
        Source/UI/PianoRoll.cpp
        Source/UI/TrackInspector.h
        Source/UI/TrackInspector.cpp
        Source/UI/TracksContainer.h
        Source/UI/TracksContainer.cpp

        Source/Tracks/TrackAudioSource.h
        Source/Tracks/TrackBase.h
        Source/Tracks/TrackBase.cpp
        Source/Tracks/MidiTrack.h
        Source/Tracks/MidiTrack.cpp
        Source/Tracks/AudioTrack.h
        Source/Tracks/AudioTrack.cpp
        Source/Tracks/TrackFactory.h
        Source/Tracks/TrackFactory.cpp

        Source/Audio/AudioEngine.h
        Source/Audio/AudioEngine.cpp
        Source/Audio/Metronome.h
        Source/Audio/Metronome.cpp

        Source/Dev/MelodyInjector.h
        Source/Dev/MelodyInjector.cpp
        Source/Dev/AudioInputSimulator.h

        Source/Project/ProjectFile.h
        Source/Project/ProjectFile.cpp

        Source/App/MainComponent.h
        Source/App/MainComponent.cpp)

target_compile_definitions(CAPT
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(CAPT
    PRIVATE
        juce::juce_gui_extra
        juce::juce_audio_utils
        "-framework AVFoundation"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)