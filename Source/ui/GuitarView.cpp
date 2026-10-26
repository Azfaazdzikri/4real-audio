#include "GuitarView.h"

#include "Photos.h"

namespace fourreal::ui
{
GuitarView::GuitarView (FourRealAudioProcessor& p) : processor (p)
{
    setGuitar (p.selectedGuitar());
    startTimerHz (60);
}

GuitarView::~GuitarView() { stopTimer(); }

void GuitarView::setGuitar (int index)
{
    guitar = index;
    const auto& entry = FourRealAudioProcessor::library().at (index);
    model = entry.guitar;
    scene = &sceneFor (entry.code());
    accent = juce::Colours::white;
    hover = drag = {};
    shownSlot = slotOfPosition (selectorPosition());
    previousPosition = -1;
    lastPosition = -1;
    fade = 1.0f;
    knobStrip[0] = embeddedImage (juce::String (model->code) + "_knob_vol.png");
    knobStrip[1] = embeddedImage (juce::String (model->code) + "_knob_tone.png");
    knobFrame = knobStrip[0].isValid() ? knobStrip[0].getWidth() : 0;
    knobHalf = scene->knobs.front().r + 12.0f;
    resized();
    repaint();
}

void GuitarView::resized()
{
    const auto b = getLocalBounds().toFloat();
    scale = juce::jmin (b.getWidth() / kPhotoW, b.getHeight() / kPhotoH);
    offset = { (b.getWidth() - kPhotoW * scale) * 0.5f, (b.getHeight() - kPhotoH * scale) * 0.5f };
    scaledPictures.assign (model->positions.size(), {});   // rebuilt on demand at the new size
}

const juce::Image& GuitarView::pictureFor (int position)
{
    position = juce::jlimit (0, static_cast<int> (model->positions.size()) - 1, position);
    auto& img = scaledPictures[static_cast<size_t> (position)];
    if (! img.isValid())
    {
        const auto& src = embeddedImage (juce::String (model->code) + "_pos" + juce::String (position) + ".jpg");
        if (src.isValid())
        {
            const float dpi = juce::Component::getApproximateScaleFactorForComponent (this);
            const int w = juce::roundToInt (kPhotoW * scale * dpi), h = juce::roundToInt (kPhotoH * scale * dpi);
            if (w > 0 && h > 0)
                img = src.rescaled (w, h, juce::Graphics::highResamplingQuality);
        }
    }
    return img;
}

// --- parameters ---------------------------------------------------------------------------------------------------------
juce::RangedAudioParameter* GuitarView::knobParam (int knob) const
{
    const auto& k = scene->knobs[static_cast<size_t> (knob)];
    return processor.apvts.getParameter (FourRealAudioProcessor::guitarParamId (
        guitar, k.kind == KnobKind::Volume ? "vol" : "tone", k.index));
}

float GuitarView::knobValue (int knob) const
{
    if (auto* p = knobParam (knob))
        return p->getValue();
    return 1.0f;
}

void GuitarView::setKnobValue (int knob, float v, bool gesture)
{
    if (auto* p = knobParam (knob))
    {
        if (gesture)
            p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
        if (gesture)
            p->endChangeGesture();
    }
}

juce::String GuitarView::knobTitle (int knob) const
{
    const auto& k = scene->knobs[static_cast<size_t> (knob)];
    if (k.kind == KnobKind::Volume)
        return k.index < static_cast<int> (model->volumes.size()) ? juce::String (model->volumes[static_cast<size_t> (k.index)].label) : juce::String();
    return k.index < static_cast<int> (model->tones.size()) ? juce::String (model->tones[static_cast<size_t> (k.index)].label) : juce::String();
}

int GuitarView::selectorPosition() const
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (FourRealAudioProcessor::guitarParamId (guitar, "sel"))))
        return p->getIndex();
    return 0;
}

int GuitarView::positionOfSlot (int slot) const
{
    if (slot < 0 || slot >= scene->selector.numSlots())
        return -1;
    const unsigned mask = scene->selector.slotMasks[static_cast<size_t> (slot)];
    for (size_t i = 0; i < model->positions.size(); ++i)
        if (model->positions[i].pickupMask == mask)
            return static_cast<int> (i);
    return -1;
}

float GuitarView::slotOfPosition (int position) const
{
    if (position < 0 || position >= static_cast<int> (model->positions.size()))
        return 0.0f;
    const unsigned mask = model->positions[static_cast<size_t> (position)].pickupMask;
    for (int s = 0; s < scene->selector.numSlots(); ++s)
        if (scene->selector.slotMasks[static_cast<size_t> (s)] == mask)
            return static_cast<float> (s);
    return 0.0f;
}

void GuitarView::setPosition (int position)
{
    if (position < 0 || position == selectorPosition())
        return;
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (FourRealAudioProcessor::guitarParamId (guitar, "sel"))))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (position)));
        p->endChangeGesture();
    }
}

// --- painting -----------------------------------------------------------------------------------------------------------
void GuitarView::timerCallback()
{
    const int pos = selectorPosition();
    bool dirty = needsRepaint;
    if (pos != lastPosition)
    {
        previousPosition = lastPosition;   // short cross-fade between the two pictures
        fade = previousPosition < 0 ? 1.0f : 0.0f;
        lastPosition = pos;
        dirty = true;
    }
    if (fade < 1.0f)
    {
        fade = juce::jmin (1.0f, fade + 0.25f);
        dirty = true;
    }
    for (size_t k = 0; k < scene->knobs.size() && k < 16; ++k)
    {
        const float v = knobValue (static_cast<int> (k));
        if (v != lastShown[k])
        {
            lastShown[k] = v;
            dirty = true;
        }
    }
    if (dirty)
    {
        needsRepaint = false;
        repaint();
    }
}

void GuitarView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101216));
    const int pos = selectorPosition();
    const juce::Rectangle<float> area (offset.x, offset.y, kPhotoW * scale, kPhotoH * scale);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (fade < 1.0f && previousPosition >= 0)
    {
        g.drawImage (pictureFor (previousPosition), area);
        g.setOpacity (fade);
    }
    g.drawImage (pictureFor (pos), area);
    g.setOpacity (1.0f);

    // knobs: the frame of the filmstrip for the value (they really turn)
    for (size_t i = 0; i < scene->knobs.size(); ++i)
    {
        const auto& k = scene->knobs[i];
        const auto& strip = knobStrip[k.kind == KnobKind::Volume ? 0 : 1];
        if (! strip.isValid() || knobFrame <= 0)
            continue;
        const int frames = strip.getHeight() / knobFrame;
        const float f = knobHalf * (k.r / scene->knobs.front().r);   // knobs of one guitar can differ a little in size
        const int frame = juce::jlimit (0, frames - 1, juce::roundToInt (knobValue (static_cast<int> (i)) * static_cast<float> (frames - 1)));
        const juce::Rectangle<float> dst (offset.x + (k.x - f) * scale, offset.y + (k.y - f) * scale, 2 * f * scale, 2 * f * scale);
        g.drawImage (strip, dst.getX(), dst.getY(), dst.getWidth(), dst.getHeight(), 0, frame * knobFrame, knobFrame, knobFrame);
    }

    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::scale (scale).translated (offset));

    std::vector<bool> available;
    for (int s = 0; s < scene->selector.numSlots(); ++s)
        available.push_back (positionOfSlot (s) >= 0);
    if (hover.hit == Hit::Selector || drag.hit == Hit::Selector)
        paintSelectorHints (g, scene->selector, available, accent);

    for (size_t k = 0; k < scene->knobs.size(); ++k)
        paintKnob (g, scene->knobs[k], knobValue (static_cast<int> (k)),
                   hover.hit == Hit::Knob && hover.index == static_cast<int> (k), drag.hit == Hit::Knob && drag.index == static_cast<int> (k), accent);

    const Target& t = drag.hit != Hit::None ? drag : hover;
    if (t.hit == Hit::Knob)
    {
        const auto& k = scene->knobs[static_cast<size_t> (t.index)];
        paintTag (g, { k.x, k.y - k.r }, knobTitle (t.index), juce::String (knobValue (t.index) * 10.0f, 1), 1.0f / scale, accent);
    }
    else if (t.hit == Hit::Selector)
    {
        const int pos = selectorPosition();
        paintTag (g, scene->selector.pivot - juce::Point<float> (0.0f, scene->selector.length), "Pickup",
                  pos < static_cast<int> (model->positions.size()) ? model->positions[static_cast<size_t> (pos)].name : "", 1.0f / scale, accent);
    }
}

// --- mouse --------------------------------------------------------------------------------------------------------------
GuitarView::Target GuitarView::hitTest (juce::Point<float> p) const
{
    for (size_t k = 0; k < scene->knobs.size(); ++k)
        if (p.getDistanceFrom ({ scene->knobs[k].x, scene->knobs[k].y }) <= scene->knobs[k].r + 5.0f)
            return { Hit::Knob, static_cast<int> (k) };
    if (scene->selector.hitBox().contains (p))
        return { Hit::Selector, 0 };
    for (size_t i = 0; i < scene->pickups.size(); ++i)
        if (scene->pickups[i].box.contains (p))
            return { Hit::Pickup, static_cast<int> (i) };
    return {};
}

void GuitarView::mouseMove (const juce::MouseEvent& e)
{
    const auto t = hitTest (toPhoto (e.position));
    if (t.hit != hover.hit || t.index != hover.index)
    {
        hover = t;
        setMouseCursor (t.hit == Hit::None ? juce::MouseCursor::NormalCursor
                        : t.hit == Hit::Knob ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void GuitarView::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void GuitarView::mouseDown (const juce::MouseEvent& e)
{
    drag = hitTest (toPhoto (e.position));
    if (drag.hit == Hit::Knob)
    {
        dragStartValue = knobValue (drag.index);
        if (auto* p = knobParam (drag.index))
            p->beginChangeGesture();
    }
    else if (drag.hit == Hit::Selector)
        mouseDrag (e);
    else if (drag.hit == Hit::Pickup)
    {
        // a pickup is chosen by the selector position that connects exactly this pickup
        const int pickup = scene->pickups[static_cast<size_t> (drag.index)].pickup;
        for (size_t i = 0; i < model->positions.size(); ++i)
            if (model->positions[i].pickupMask == (1u << pickup))
                setPosition (static_cast<int> (i));
        drag = {};
    }
    repaint();
}

void GuitarView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.hit == Hit::Knob)
    {
        const float dy = static_cast<float> (e.getDistanceFromDragStartY()) / scale;
        const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
        if (auto* p = knobParam (drag.index))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, dragStartValue - dy / 160.0f * fine));
    }
    else if (drag.hit == Hit::Selector)
    {
        // the nearest slot that was recorded
        const int want = scene->selector.slotAt (toPhoto (e.position));
        int best = -1, bestDist = 1000;
        for (int s = 0; s < scene->selector.numSlots(); ++s)
            if (positionOfSlot (s) >= 0 && std::abs (s - want) < bestDist)
            {
                bestDist = std::abs (s - want);
                best = s;
            }
        setPosition (positionOfSlot (best));
    }
    repaint();
}

void GuitarView::mouseUp (const juce::MouseEvent&)
{
    if (drag.hit == Hit::Knob)
        if (auto* p = knobParam (drag.index))
            p->endChangeGesture();
    drag = {};
    repaint();
}

void GuitarView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto t = hitTest (toPhoto (e.position));
    if (t.hit == Hit::Knob)
        setKnobValue (t.index, 1.0f, true); // 10
}

void GuitarView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto t = hitTest (toPhoto (e.position));
    if (t.hit == Hit::Knob)
        setKnobValue (t.index, knobValue (t.index) + w.deltaY * 0.2f * (w.isReversed ? -1.0f : 1.0f), true);
}
}
