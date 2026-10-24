#include "GuitarScene.h"

#include <cmath>
#include <map>

namespace fourreal::ui
{
using juce::Colour;
using juce::Point;
using juce::Rectangle;

namespace
{
constexpr float kPi = juce::MathConstants<float>::pi;

Point<float> unit (float x, float y)
{
    const float n = std::sqrt (x * x + y * y);
    return { x / n, y / n };
}

// Rotates a vector in screen coordinates (y down): a positive angle turns it clockwise on the screen.
Point<float> rotated (Point<float> v, float degrees)
{
    const float a = juce::degreesToRadians (degrees), c = std::cos (a), s = std::sin (a);
    return { v.x * c - v.y * s, v.x * s + v.y * c };
}

// ------------------------------------------------------------------------------------------------------------------
// The numbers below are pixels in the 1376 x 768 photos. Pickup order everywhere: 0 = bridge, last = neck.
// Selector slot masks: bit i = pickup i is connected. 3-way: Bridge, Both, Neck. 5-way Strat: Bridge, Bridge+Middle,
// Middle, Middle+Neck, Neck.
// ------------------------------------------------------------------------------------------------------------------
constexpr unsigned kB = 1u, kBoth = 3u, kN = 2u;                    // two pickups
constexpr unsigned kSB = 1u, kSBM = 3u, kSM = 2u, kSMN = 6u, kSN = 4u; // three pickups

std::map<std::string, GuitarScene> buildScenes()
{
    std::map<std::string, GuitarScene> m;
    const Colour cream (0xfff4e8c8), creamShade (0xffb59b60);
    const Colour black (0xff1b1b1e), blackShade (0xff050506);
    const Colour chromeLight (0xffe4e6ea), chromeDark (0xff6c6f76);

    // --- Les Paul ---------------------------------------------------------------------------------------------------
    {
        GuitarScene s;
        s.code = "LP";
        s.pickups = { { { 328, 334, 56, 98 }, 0 }, { { 470, 334, 56, 98 }, 1 } };
        s.knobs = { { 148, 493, 17, KnobKind::Volume, 1 },   // VOL BRIDGE
                    { 227, 494, 17, KnobKind::Volume, 0 },   // VOL NECK
                    { 183, 555, 17, KnobKind::Tone, 1 },     // TONE BRIDGE
                    { 265, 559, 17, KnobKind::Tone, 0 } };   // TONE NECK
        auto& v = s.selector;
        v.style = LeverStyle::Toggle;
        v.pivot = { 556, 277 };
        v.direction = { 0, -1 };
        v.tiltDeg = 38;
        v.length = 18;
        v.width = 12;
        v.slotMasks = { kB, kBoth, kN };
        v.tip = Colour (0xffeaa63a);
        v.tipShade = Colour (0xff8a4c10);
        m[s.code] = s;
    }
    // --- ES-335 -----------------------------------------------------------------------------------------------------
    {
        GuitarScene s;
        s.code = "335";
        s.pickups = { { { 362, 337, 48, 96 }, 0 }, { { 500, 338, 50, 94 }, 1 } };
        s.knobs = { { 141, 556, 19, KnobKind::Volume, 1 },
                    { 220, 531, 19, KnobKind::Volume, 0 },
                    { 190, 622, 19, KnobKind::Tone, 1 },
                    { 272, 600, 19, KnobKind::Tone, 0 } };
        auto& v = s.selector;
        v.style = LeverStyle::Toggle;
        v.pivot = { 277, 509 };
        v.direction = { 0, 1 };
        v.tiltDeg = 38;
        v.length = 15;
        v.width = 11;
        v.slotMasks = { kB, kBoth, kN };
        v.tip = cream;
        v.tipShade = creamShade;
        v.nutRadius = 11.5f;
        v.nutLight = chromeLight;
        v.nutDark = chromeDark;
        m[s.code] = s;
    }
    // --- SG ---------------------------------------------------------------------------------------------------------
    {
        GuitarScene s;
        s.code = "SG";
        s.pickups = { { { 343, 340, 48, 90 }, 0 }, { { 463, 340, 48, 90 }, 1 } };
        s.knobs = { { 133, 492, 18, KnobKind::Volume, 1 },
                    { 192, 509, 18, KnobKind::Volume, 0 },
                    { 131, 548, 18, KnobKind::Tone, 1 },
                    { 192, 565, 18, KnobKind::Tone, 0 } };
        auto& v = s.selector;
        v.style = LeverStyle::Toggle;
        v.pivot = { 258, 531 };
        v.direction = { 0, 1 };
        v.tiltDeg = 36;
        v.length = 17;
        v.width = 12;
        v.slotMasks = { kB, kBoth, kN };
        v.tip = cream;
        v.tipShade = creamShade;
        v.nutRadius = 11.0f;
        v.nutLight = chromeLight;
        v.nutDark = chromeDark;
        m[s.code] = s;
    }
    // --- PRS SE Custom 24 -------------------------------------------------------------------------------------------
    {
        GuitarScene s;
        s.code = "PRS";
        s.pickups = { { { 315, 330, 58, 115 }, 0 }, { { 438, 330, 58, 115 }, 1 } };
        s.knobs = { { 162, 465, 16, KnobKind::Volume, 0 },
                    { 306, 500, 16, KnobKind::Tone, 0 } };
        auto& v = s.selector;
        v.style = LeverStyle::Blade;
        v.pivot = { 199, 531 };
        v.slotEnd = { 229, 516 };
        v.direction = unit (-0.89f, 0.46f);
        v.length = 22;
        v.width = 11;
        v.slotMasks = { kB, kBoth, kN };
        v.tip = cream;
        v.tipShade = creamShade;
        v.drawSlot = true;
        m[s.code] = s;
    }
    // --- Rickenbacker 330 -------------------------------------------------------------------------------------------
    {
        GuitarScene s;
        s.code = "RIC";
        s.pickups = { { { 316, 324, 52, 122 }, 0 }, { { 452, 324, 50, 122 }, 1 } };
        s.knobs = { { 209, 506, 19, KnobKind::Volume, 1 },
                    { 271, 506, 19, KnobKind::Volume, 0 },
                    { 209, 568, 19, KnobKind::Tone, 1 },
                    { 271, 568, 19, KnobKind::Tone, 0 } };
        auto& v = s.selector;
        v.style = LeverStyle::Toggle;
        v.pivot = { 317, 539 };
        v.direction = { 0, 1 };
        v.tiltDeg = 38;
        v.length = 13;
        v.width = 10;
        v.slotMasks = { kB, kBoth, kN };
        v.tip = black;
        v.tipShade = blackShade;
        v.nutRadius = 12.5f;
        v.nutLight = chromeLight;
        v.nutDark = chromeDark;
        m[s.code] = s;
    }
    // --- Stratocaster -----------------------------------------------------------------------------------------------
    {
        GuitarScene s;
        s.code = "STR";
        s.pickups = { { { 304, 344, 32, 96 }, 0 }, { { 381, 345, 30, 96 }, 1 }, { { 461, 346, 30, 96 }, 2 } };
        s.knobs = { { 307, 466, 18, KnobKind::Volume, 0 },
                    { 278, 520, 18, KnobKind::Tone, 0 },    // TONE NECK
                    { 240, 567, 18, KnobKind::Tone, 1 } };  // TONE MIDDLE
        auto& v = s.selector;
        v.style = LeverStyle::Blade;
        v.pivot = { 331, 529 };
        v.slotEnd = { 350, 510 };
        v.direction = unit (-0.97f, 0.25f);
        v.length = 18;
        v.width = 9;
        v.slotMasks = { kSB, kSBM, kSM, kSMN, kSN };
        v.tip = cream;
        v.tipShade = creamShade;
        v.drawSlot = false; // the slot of the photo is intact
        m[s.code] = s;
    }
    // --- Telecaster -------------------------------------------------------------------------------------------------
    {
        GuitarScene s;
        s.code = "TCR";
        s.pickups = { { { 276, 338, 50, 96 }, 0 }, { { 452, 342, 22, 88 }, 1 } };
        s.knobs = { { 145, 516, 17, KnobKind::Volume, 0 },
                    { 233, 515, 17, KnobKind::Tone, 0 } };
        auto& v = s.selector;
        v.style = LeverStyle::Blade;
        v.pivot = { 284, 517 };
        v.slotEnd = { 311, 520 };
        v.direction = unit (-1.0f, -0.05f);
        v.length = 27;
        v.width = 14;
        v.slotMasks = { kB, kBoth, kN };
        v.tip = black;
        v.tipShade = blackShade;
        v.drawSlot = true;
        m[s.code] = s;
    }
    // --- Gretsch White Falcon ---------------------------------------------------------------------------------------
    {
        GuitarScene s;
        s.code = "FLC";
        s.pickups = { { { 382, 322, 58, 108 }, 0 }, { { 512, 322, 58, 108 }, 1 } };
        s.knobs = { { 262, 494, 15, KnobKind::Volume, 1 },  // VOL BRIDGE
                    { 598, 529, 15, KnobKind::Volume, 0 },  // VOL NECK
                    { 205, 551, 15, KnobKind::Volume, 2 },  // MASTER
                    { 265, 621, 15, KnobKind::Tone, 0 } };
        auto& v = s.selector;
        v.style = LeverStyle::Toggle;
        v.pivot = { 651, 259 };
        v.direction = { 0, 1 };
        v.tiltDeg = 38;
        v.length = 15;
        v.width = 11;
        v.slotMasks = { kB, kBoth, kN };
        v.tip = Colour (0xffe0bd55);
        v.tipShade = Colour (0xff7d5c14);
        v.nutRadius = 10.5f;
        v.nutLight = Colour (0xfff0d77a);
        v.nutDark = Colour (0xff8a6516);
        m[s.code] = s;
    }
    return m;
}

void strokeLine (juce::Graphics& g, Point<float> a, Point<float> b, float w)
{
    g.drawLine (juce::Line<float> (a, b), w);
}
} // namespace

// --- SelectorSpot ----------------------------------------------------------------------------------------------------
Point<float> SelectorSpot::slotPoint (float slot) const
{
    const float t = numSlots() > 1 ? juce::jlimit (0.0f, 1.0f, slot / static_cast<float> (numSlots() - 1)) : 0.0f;
    return pivot + (slotEnd - pivot) * t;
}

Rectangle<float> SelectorSpot::hitBox() const
{
    if (style == LeverStyle::Toggle)
    {
        const float r = length + width;
        return { pivot.x - r, pivot.y - r, 2 * r, 2 * r };
    }
    Rectangle<float> r (pivot, slotEnd);
    r = r.getUnion (Rectangle<float> (pivot + direction * length, slotEnd + direction * length));
    return r.expanded (width * 0.7f + 4.0f);
}

int SelectorSpot::slotAt (Point<float> p) const
{
    const int n = numSlots();
    if (n <= 1)
        return 0;
    float t;
    if (style == LeverStyle::Toggle)
    {
        t = juce::jlimit (-1.0f, 1.0f, (p.x - pivot.x) / (length * 0.9f)) * 0.5f + 0.5f;
    }
    else
    {
        const auto axis = slotEnd - pivot;
        t = ((p - pivot).x * axis.x + (p - pivot).y * axis.y) / (axis.x * axis.x + axis.y * axis.y);
    }
    return juce::jlimit (0, n - 1, juce::roundToInt (t * static_cast<float> (n - 1)));
}

const GuitarScene& sceneFor (const std::string& code)
{
    static const auto scenes = buildScenes();
    auto it = scenes.find (code);
    return it != scenes.end() ? it->second : scenes.begin()->second;
}

// --- painting --------------------------------------------------------------------------------------------------------
void paintPickup (juce::Graphics& g, const PickupSpot& p, bool selected, bool hovered, Colour accent)
{
    const auto r = p.box;
    if (selected)
    {
        for (int i = 3; i >= 1; --i) // soft glow
        {
            g.setColour (accent.withAlpha (0.07f * static_cast<float> (4 - i)));
            g.drawRoundedRectangle (r.expanded (static_cast<float> (i) * 2.2f), 7.0f + static_cast<float> (i) * 2.0f, 2.2f);
        }
        g.setColour (accent.withAlpha (0.20f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (accent.withAlpha (0.95f));
        g.drawRoundedRectangle (r.expanded (1.0f), 7.0f, 2.6f);
    }
    else if (hovered)
    {
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.drawRoundedRectangle (r.expanded (1.0f), 7.0f, 1.8f);
    }
}

void paintKnob (juce::Graphics& g, const KnobSpot& k, float v, bool hovered, bool dragging, Colour accent)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    const float a0 = -0.75f * kPi, a1 = 0.75f * kPi, a = a0 + v * (a1 - a0);

    if (hovered || dragging)
    {
        g.setColour (juce::Colours::white.withAlpha (dragging ? 0.20f : 0.13f));
        g.fillEllipse (k.x - k.r, k.y - k.r, 2 * k.r, 2 * k.r);
    }

    const Point<float> c (k.x, k.y), d (std::sin (a), -std::cos (a));
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    strokeLine (g, c + d * (k.r * 0.22f), c + d * (k.r * 0.95f), 4.0f);
    g.setColour (juce::Colours::white);
    strokeLine (g, c + d * (k.r * 0.22f), c + d * (k.r * 0.95f), 2.0f);

    // only the number 0.0 .. 10.0, under the knob
    const juce::String num (v * 10.0f, 1);
    const Point<float> tc (k.x, k.y + k.r + 11.0f);
    const juce::Rectangle<float> pill (tc.x - 17.0f, tc.y - 8.5f, 34.0f, 17.0f);
    g.setColour (juce::Colours::black.withAlpha (dragging || hovered ? 0.82f : 0.62f));
    g.fillRoundedRectangle (pill, 8.5f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font { juce::FontOptions (12.5f, juce::Font::bold) });
    g.drawText (num, pill, juce::Justification::centred, false);
}

void paintSelector (juce::Graphics& g, const SelectorSpot& s, float slot, const std::vector<bool>& available, bool hovered,
                    Colour accent)
{
    const int n = s.numSlots();
    slot = juce::jlimit (0.0f, static_cast<float> (juce::jmax (0, n - 1)), slot);
    const Point<float> shadow (2.0f, 3.2f);

    auto tipDir = [&s, n] (float sl)
    {
        const float mid = static_cast<float> (n - 1) * 0.5f;
        const float side = s.direction.y <= 0 ? 1.0f : -1.0f; // a larger slot always moves the tip toward +x (the neck)
        return rotated (s.direction, side * s.tiltDeg * (sl - mid) / juce::jmax (0.5f, mid));
    };

    if (s.style == LeverStyle::Toggle)
    {
        if (hovered)
        {
            g.setColour (accent.withAlpha (0.9f));
            g.drawEllipse (s.pivot.x - s.length - 8, s.pivot.y - s.length - 8, 2 * (s.length + 8), 2 * (s.length + 8), 2.0f);
            for (int i = 0; i < n; ++i)
            {
                const auto p = s.pivot + tipDir (static_cast<float> (i)) * (s.length + 14.0f);
                const bool ok = i < static_cast<int> (available.size()) && available[static_cast<size_t> (i)];
                g.setColour (ok ? juce::Colours::white.withAlpha (0.9f) : juce::Colours::black.withAlpha (0.55f));
                g.fillEllipse (p.x - 3.2f, p.y - 3.2f, 6.4f, 6.4f);
            }
        }
        const auto dir = tipDir (slot);
        const auto tip = s.pivot + dir * s.length;
        if (s.nutRadius > 0.0f) // mounting ring under the lever (the photo's own ring was damaged when the lever was erased)
        {
            const float r = s.nutRadius;
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (s.pivot.x - r + 1.5f, s.pivot.y - r + 2.5f, 2 * r, 2 * r);
            g.setGradientFill (juce::ColourGradient (s.nutLight, s.pivot.x - r * 0.7f, s.pivot.y - r * 0.8f, s.nutDark,
                                                     s.pivot.x + r * 0.8f, s.pivot.y + r * 0.9f, false));
            g.fillEllipse (s.pivot.x - r, s.pivot.y - r, 2 * r, 2 * r);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawEllipse (s.pivot.x - r, s.pivot.y - r, 2 * r, 2 * r, 1.0f);
            g.setGradientFill (juce::ColourGradient (s.nutDark.darker (0.4f), s.pivot.x - r * 0.4f, s.pivot.y - r * 0.5f,
                                                     s.nutLight.withAlpha (0.8f), s.pivot.x + r * 0.5f, s.pivot.y + r * 0.5f, false));
            g.fillEllipse (s.pivot.x - r * 0.62f, s.pivot.y - r * 0.62f, r * 1.24f, r * 1.24f);
        }
        // hub of the lever
        g.setColour (juce::Colour (0xff25262a));
        g.fillEllipse (s.pivot.x - s.width * 0.34f, s.pivot.y - s.width * 0.34f, s.width * 0.68f, s.width * 0.68f);
        g.setColour (juce::Colour (0xffb9bcc3));
        g.drawEllipse (s.pivot.x - s.width * 0.34f, s.pivot.y - s.width * 0.34f, s.width * 0.68f, s.width * 0.68f, 1.2f);
        // shaft
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        strokeLine (g, s.pivot + shadow, tip + shadow, s.width * 0.42f);
        g.setColour (juce::Colour (0xff9fa2a9));
        strokeLine (g, s.pivot, tip, s.width * 0.40f);
        g.setColour (juce::Colour (0xffe9ebef));
        strokeLine (g, s.pivot, tip, s.width * 0.14f);
        // tip: a rounded cap, long along the lever
        const float along = s.width * 0.95f, across = s.width * 0.78f;
        const auto c = tip + dir * (s.width * 0.18f);
        juce::Path cap;
        cap.addEllipse (-along * 0.5f, -across * 0.5f, along, across);
        const auto xf = juce::AffineTransform::rotation (std::atan2 (dir.y, dir.x)).translated (c.x, c.y);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillPath (cap, xf.translated (shadow.x, shadow.y));
        juce::ColourGradient gr (s.tip.brighter (0.35f), c.x - along * 0.15f, c.y - across * 0.25f, s.tipShade, c.x + along * 0.5f,
                                 c.y + across * 0.5f, true);
        gr.addColour (0.55, s.tip);
        g.setGradientFill (gr);
        g.fillPath (cap, xf);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.strokePath (cap, juce::PathStrokeType (1.0f), xf);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillEllipse (c.x - along * 0.22f, c.y - across * 0.30f, along * 0.28f, across * 0.22f);
    }
    else // Blade
    {
        const auto p0 = s.slotPoint (0), p1 = s.slotPoint (static_cast<float> (n - 1));
        if (s.drawSlot)
        {
            g.setColour (juce::Colour (0xff0b0b0c));
            strokeLine (g, p0, p1, 3.6f);
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            strokeLine (g, p0 + Point<float> (0.6f, 1.6f), p1 + Point<float> (0.6f, 1.6f), 1.0f);
        }
        if (hovered)
        {
            g.setColour (accent.withAlpha (0.9f));
            g.drawRoundedRectangle (s.hitBox(), 6.0f, 1.8f);
            for (int i = 0; i < n; ++i)
            {
                const auto p = s.slotPoint (static_cast<float> (i)) + Point<float> (s.direction.y * -1.0f, s.direction.x) * 11.0f;
                const bool ok = i < static_cast<int> (available.size()) && available[static_cast<size_t> (i)];
                g.setColour (ok ? juce::Colours::white.withAlpha (0.9f) : juce::Colours::black.withAlpha (0.55f));
                g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
            }
        }
        const auto start = s.slotPoint (slot);
        const auto mid = start + s.direction * (s.length * 0.5f);
        juce::Path body;
        body.addRoundedRectangle (-s.length * 0.5f, -s.width * 0.5f, s.length, s.width, s.width * 0.45f);
        const auto xf = juce::AffineTransform::rotation (std::atan2 (s.direction.y, s.direction.x)).translated (mid.x, mid.y);
        g.setColour (juce::Colours::black.withAlpha (0.32f));
        g.fillPath (body, xf.translated (shadow.x, shadow.y));
        juce::ColourGradient gr (s.tip.brighter (0.3f), mid.x, mid.y - s.width * 0.5f, s.tipShade, mid.x, mid.y + s.width * 0.5f, false);
        gr.addColour (0.4, s.tip);
        g.setGradientFill (gr);
        g.fillPath (body, xf);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.strokePath (body, juce::PathStrokeType (1.0f), xf);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.fillEllipse (mid.x - s.length * 0.28f, mid.y - s.width * 0.34f, s.length * 0.36f, s.width * 0.22f);
    }
}

void paintTag (juce::Graphics& g, Point<float> anchor, const juce::String& title, const juce::String& value, float pixel,
               Colour accent)
{
    const float fs = 12.5f * pixel;
    const juce::Font f1 { juce::FontOptions (fs) };
    const juce::Font f2 { juce::FontOptions (fs, juce::Font::bold) };
    auto width = [] (const juce::Font& f, const juce::String& t)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, t, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    };
    const float w1 = width (f1, title), w2 = width (f2, value);
    const float padX = 8.0f * pixel, gap = 7.0f * pixel, h = 22.0f * pixel;
    const float w = padX * 2 + w1 + gap + w2;
    Rectangle<float> r (anchor.x - w * 0.5f, anchor.y - h - 12.0f * pixel, w, h);
    r.setPosition (juce::jlimit (4.0f * pixel, kPhotoW - w - 4.0f * pixel, r.getX()), juce::jmax (4.0f * pixel, r.getY()));
    g.setColour (juce::Colour (0xe6101216));
    g.fillRoundedRectangle (r, h * 0.5f);
    g.setColour (accent.withAlpha (0.9f));
    g.drawRoundedRectangle (r, h * 0.5f, 1.2f * pixel);
    g.setFont (f1);
    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.drawText (title, juce::Rectangle<float> (r.getX() + padX, r.getY(), w1 + 2, h), juce::Justification::centredLeft, false);
    g.setFont (f2);
    g.setColour (juce::Colours::white);
    g.drawText (value, juce::Rectangle<float> (r.getX() + padX + w1 + gap, r.getY(), w2 + 2, h), juce::Justification::centredLeft,
                false);
}
} // namespace fourreal::ui
