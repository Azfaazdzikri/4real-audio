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
    accent = juce::Colour (0xff000000u | model->accentRgb);
    hover = drag = {};
    shownSlot = slotOfPosition (selectorPosition());
    scaled = {};
    resized();
    repaint();
}

void GuitarView::resized()
{
    const auto& photo = guitarPhoto (model->code);
    const auto b = getLocalBounds().toFloat();
    scale = juce::jmin (b.getWidth() / kPhotoW, b.getHeight() / kPhotoH);
    offset = { (b.getWidth() - kPhotoW * scale) * 0.5f, (b.getHeight() - kPhotoH * scale) * 0.5f };
    scaled = {};
    if (photo.isValid())
    {
        const float dpi = juce::Component::getApproximateScaleFactorForComponent (this);
        const int w = juce::roundToInt (kPhotoW * scale * dpi), h = juce::roundToInt (kPhotoH * scale * dpi);
        if (w > 0 && h > 0)
            scaled = photo.rescaled (w, h, juce::Graphics::highResamplingQuality);
    }
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
    const float target = slotOfPosition (pos);
    bool dirty = needsRepaint;
    if (std::abs (shownSlot - target) > 0.002f)
    {
        shownSlot += (target - shownSlot) * 0.35f;
        if (std::abs (shownSlot - target) < 0.01f)
            shownSlot = target;
        dirty = true;
    }
    if (pos != lastPosition)
    {
        lastPosition = pos;
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
    if (scaled.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (scaled, juce::Rectangle<float> (offset.x, offset.y, kPhotoW * scale, kPhotoH * scale));
    }
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::scale (scale).translated (offset));

    const unsigned mask = model->positions.empty() ? 0u : model->positions[static_cast<size_t> (juce::jlimit (0, static_cast<int> (model->positions.size()) - 1, selectorPosition()))].pickupMask;
    for (const auto& pk : scene->pickups)
        paintPickup (g, pk, (mask >> pk.pickup) & 1u, hover.hit == Hit::Pickup && scene->pickups[static_cast<size_t> (hover.index)].pickup == pk.pickup, accent);

    std::vector<bool> available;
    for (int s = 0; s < scene->selector.numSlots(); ++s)
        available.push_back (positionOfSlot (s) >= 0);
    paintSelector (g, scene->selector, shownSlot, available, hover.hit == Hit::Selector || drag.hit == Hit::Selector, accent);

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
