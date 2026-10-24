#include "LibraryBrowser.h"

#include "Photos.h"

namespace fourreal::ui
{
LibraryBrowser::LibraryBrowser (FourRealAudioProcessor& p) : processor (p) { selected = p.selectedGuitar(); }

juce::Rectangle<int> LibraryBrowser::cardBounds (int i) const
{
    return { 0, i * (kCardH + kGap), getWidth(), kCardH };
}

int LibraryBrowser::cardAt (juce::Point<int> p) const
{
    for (int i = 0; i < processor.numGuitars(); ++i)
        if (cardBounds (i).contains (p))
            return i;
    return -1;
}

void LibraryBrowser::paint (juce::Graphics& g)
{
    for (int i = 0; i < processor.numGuitars(); ++i)
    {
        const auto& entry = FourRealAudioProcessor::library().at (i);
        const auto r = cardBounds (i);
        const auto accent = juce::Colour (0xff000000u | entry.guitar->accentRgb);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (r);
        g.setColour (juce::Colour (0xff1d2027));
        g.fillRoundedRectangle (r.toFloat(), 8.0f);

        const auto& photo = guitarPhoto (entry.code());
        if (photo.isValid())
        {
            // centre band of the photo that has the aspect of the card
            const int ch = juce::roundToInt (photo.getWidth() * static_cast<float> (r.getHeight()) / static_cast<float> (r.getWidth()));
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.setOpacity (processor.isGuitarUnlocked (i) ? 1.0f : 0.45f);
            g.drawImage (photo, r.getX(), r.getY(), r.getWidth(), r.getHeight(), 0, (photo.getHeight() - ch) / 2, photo.getWidth(), ch);
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, static_cast<float> (r.getY()) + 28.0f,
                                                 juce::Colour (0xcc000000), 0.0f, static_cast<float> (r.getBottom()), false));
        g.fillRect (r);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font { juce::FontOptions (14.0f, juce::Font::bold) });
        g.drawText (entry.displayName(), r.reduced (10, 5), juce::Justification::bottomLeft, true);
        if (! processor.isGuitarUnlocked (i))
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::Font { juce::FontOptions (11.0f, juce::Font::bold) });
            g.drawText ("LOCKED", r.reduced (10, 6), juce::Justification::topRight, false);
        }
        g.setColour (i == selected ? accent : juce::Colours::white.withAlpha (i == hovered ? 0.5f : 0.12f));
        g.drawRoundedRectangle (r.toFloat().reduced (1.0f), 8.0f, i == selected ? 2.4f : 1.0f);
    }
}

void LibraryBrowser::mouseMove (const juce::MouseEvent& e)
{
    const int c = cardAt (e.getPosition());
    if (c != hovered)
    {
        hovered = c;
        repaint();
    }
}

void LibraryBrowser::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void LibraryBrowser::mouseDown (const juce::MouseEvent& e)
{
    const int c = cardAt (e.getPosition());
    if (c >= 0 && processor.isGuitarUnlocked (c) && onSelect)
        onSelect (c);
}
}
