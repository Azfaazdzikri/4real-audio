// Renders the overlay of every library guitar (pickups, knobs, selector lever) on its photo, to PNG files. Needs only
// juce_graphics, so it runs without a display:
//   scene_preview <folder with the photos (CODE.jpg)> <output folder>
// Writes <CODE>_full.png (1376 x 768), <CODE>_screen.png (the size used in the plugin) and _states.png (selector positions).
#include <juce_graphics/juce_graphics.h>

#include "../Source/ui/GuitarScene.h"

using namespace fourreal::ui;

static juce::Image render (const juce::Image& photo, const GuitarScene& scene, float slot, float knob, int hoverKnob, bool hoverSel,
                           int selectedMask, int outW)
{
    const float scale = static_cast<float> (outW) / kPhotoW;
    juce::Image out (juce::Image::ARGB, outW, juce::roundToInt (kPhotoH * scale), true);
    juce::Graphics g (out);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (photo, out.getBounds().toFloat());
    g.addTransform (juce::AffineTransform::scale (scale));
    const juce::Colour accent (0xff4fd1c5);
    for (const auto& p : scene.pickups)
        paintPickup (g, p, (selectedMask >> p.pickup) & 1, false, accent);
    std::vector<bool> avail (static_cast<size_t> (scene.selector.numSlots()), true);
    paintSelector (g, scene.selector, slot, avail, hoverSel, accent);
    int i = 0;
    for (const auto& k : scene.knobs)
    {
        paintKnob (g, k, knob + 0.12f * static_cast<float> (i % 3), i == hoverKnob, false, accent);
        ++i;
    }
    if (hoverKnob >= 0)
    {
        const auto& k = scene.knobs[static_cast<size_t> (hoverKnob)];
        paintTag (g, { k.x, k.y - k.r - 6 }, "VOL BRIDGE", "7.5", 1.0f / scale, accent);
    }
    return out;
}

int main (int argc, char** argv)
{
    if (argc < 3)
        return 1;
    juce::ScopedJuceInitialiser_GUI init; // fonts, message manager
    const juce::File photos (argv[1]), outDir (argv[2]);
    outDir.createDirectory();
    juce::PNGImageFormat png;
    for (const char* code : { "LP", "335", "SG", "PRS", "RIC", "STR", "TCR", "FLC" })
    {
        const auto photo = juce::ImageFileFormat::loadFrom (photos.getChildFile (juce::String (code) + ".jpg"));
        if (! photo.isValid())
        {
            std::fprintf (stderr, "missing photo %s\n", code);
            return 2;
        }
        const auto& scene = sceneFor (code);
        const int n = scene.selector.numSlots();
        auto save = [&] (const juce::Image& img, const juce::String& name)
        {
            auto f = outDir.getChildFile (name);
            f.deleteFile();
            juce::FileOutputStream os (f);
            png.writeImageToStream (img, os);
        };
        save (render (photo, scene, static_cast<float> (n / 2), 0.7f, 0, true, 3, 1376), juce::String (code) + "_full.png");
        save (render (photo, scene, static_cast<float> (n / 2), 0.7f, -1, false, 3, 908), juce::String (code) + "_screen.png");
        // all selector positions side by side, cropped around the selector
        const auto hb = scene.selector.hitBox().expanded (30.0f).getSmallestIntegerContainer();
        juce::Image strip (juce::Image::ARGB, hb.getWidth() * 3 * n, hb.getHeight() * 3, true);
        juce::Graphics sg (strip);
        for (int s = 0; s < n; ++s)
        {
            auto full = render (photo, scene, static_cast<float> (s), 0.7f, -1, true, 3, 1376);
            sg.drawImage (full.getClippedImage (hb), s * hb.getWidth() * 3, 0, hb.getWidth() * 3, hb.getHeight() * 3, 0, 0,
                          hb.getWidth(), hb.getHeight());
        }
        save (strip, juce::String (code) + "_states.png");
    }
    return 0;
}
