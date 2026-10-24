#pragma once
#include <juce_graphics/juce_graphics.h>

namespace fourreal::ui
{
// A picture embedded in the plugin by its original file name (Source/ui/photos/*.jpg, Source/ui/art/*), decoded once.
// Message thread only. Returns an invalid image if there is no such file.
const juce::Image& embeddedImage (const juce::String& fileName);
// The photo of a library guitar (Source/ui/photos/<CODE>.jpg).
inline const juce::Image& guitarPhoto (const juce::String& code) { return embeddedImage (code + ".jpg"); }
}
