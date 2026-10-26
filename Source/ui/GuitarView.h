#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "GuitarScene.h"

namespace fourreal::ui
{
// The photo of the current library guitar with live controls on it: click a pickup to select it, move the selector lever,
// turn the volume and tone knobs (drag up/down, mouse wheel, double click = 10). All controls are the parameters of the guitar.
class GuitarView : public juce::Component, private juce::Timer
{
public:
    explicit GuitarView (FourRealAudioProcessor&);
    ~GuitarView() override;

    void setGuitar (int index);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Hit { None, Knob, Pickup, Selector };
    struct Target { Hit hit = Hit::None; int index = -1; };

    void timerCallback() override;
    Target hitTest (juce::Point<float> photoPoint) const;
    juce::Point<float> toPhoto (juce::Point<float> screen) const { return (screen - offset) / scale; }
    float knobValue (int knob) const; // 0..1
    void setKnobValue (int knob, float v01, bool gesture);
    juce::RangedAudioParameter* knobParam (int knob) const;
    int selectorPosition() const;             // position index of the guitar's real selector
    float slotOfPosition (int position) const;
    int positionOfSlot (int slot) const;      // -1 = not recorded
    void setPosition (int position);
    juce::String knobTitle (int knob) const;

    FourRealAudioProcessor& processor;
    int guitar = 0;
    const GuitarScene* scene = nullptr;
    const fourreal::GuitarModel* model = nullptr;
    juce::Colour accent;

    // pre-rendered pictures (Source/ui/art): the guitar for every selector position, scaled to the screen size once
    const juce::Image& pictureFor (int position);
    std::vector<juce::Image> scaledPictures;
    int previousPosition = -1;  // the picture that fades out after a selector change
    float fade = 1.0f;          // 1 = only the current picture
    juce::Image knobStrip[2];   // filmstrips of the volume and the tone knob of this guitar
    int knobFrame = 0;          // size of one frame in the strip, pixels
    float knobHalf = 0.0f;      // half size of a frame in photo pixels
    float scale = 1.0f;
    juce::Point<float> offset;

    Target hover, drag;
    float dragStartValue = 0.0f;
    float shownSlot = 0.0f;     // animated lever position
    float lastShown[16] = {};
    int lastPosition = -1;
    bool needsRepaint = true;
    juce::Point<float> tagAnchor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GuitarView)
};
}
