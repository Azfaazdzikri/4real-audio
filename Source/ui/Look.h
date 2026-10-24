#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace fourreal::ui
{
// Dark look of the plugin. Rotary sliders: a plain dark knob with a pointer (no value arc); the number is the slider's text box.
class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::ResizableWindow::backgroundColourId, juce::Colour (0xff15171c));
        setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff23262e));
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a3f4b));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff23262e));
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2e38));
        setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override
    {
        const auto b = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y), static_cast<float> (w),
                                               static_cast<float> (h)).reduced (4.0f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4f5c), c.x - r * 0.4f, c.y - r * 0.5f,
                                                 juce::Colour (0xff1c1e24), c.x + r * 0.6f, c.y + r * 0.7f, true));
        g.fillEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawEllipse (c.x - r, c.y - r, 2 * r, 2 * r, 1.5f);
        const float a = a0 + pos * (a1 - a0);
        const juce::Point<float> d (std::sin (a), -std::cos (a));
        g.setColour (juce::Colours::white);
        g.drawLine (juce::Line<float> (c + d * (r * 0.3f), c + d * (r * 0.92f)), 3.0f);
    }
};
}
