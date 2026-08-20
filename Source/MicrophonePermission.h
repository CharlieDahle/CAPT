#pragma once

#include <functional>

// Explicitly triggers the macOS microphone-permission prompt via AVFoundation.
// JUCE's CoreAudio backend opens the input device directly without ever
// calling this, which means the OS may never ask - and the app never shows
// up under Privacy & Security > Microphone at all.
void requestMicrophonePermission (std::function<void (bool granted)> callback);
