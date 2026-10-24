#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

namespace fourreal::ui
{
// The guitar library: one card per guitar (photo, name, lock when the guitar is not licensed). Click to switch.
class LibraryBrowser : public juce::Component
{
public:
    explicit LibraryBrowser (FourRealAudioProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (int)> onSelect;
    void setSelected (int index) { if (index != selected) { selected = index; repaint(); } }

    static constexpr int kCardH = 74, kGap = 6;

private:
    juce::Rectangle<int> cardBounds (int i) const;
    int cardAt (juce::Point<int>) const;
    FourRealAudioProcessor& processor;
    int selected = 0, hovered = -1;
};
}
