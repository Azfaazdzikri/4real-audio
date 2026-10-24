#pragma once
#include <juce_graphics/juce_graphics.h>

namespace fourreal::ui
{
// The photo of a library guitar (embedded in the plugin, Source/ui/photos/<CODE>.jpg), decoded once. Message thread only.
const juce::Image& guitarPhoto (const juce::String& code);
}
