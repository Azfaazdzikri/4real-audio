// Renders the pictures the plugin shows (Source/ui/art), from the prepared photos (Source/ui/photos):
//   <CODE>_pos<i>.jpg   the guitar with the selector lever and the lit pickups of selector position i
//   <CODE>_knob.png     filmstrip of the knob of that guitar: kFrames frames, top to bottom, knob fully down .. fully up
// Usage: render_art <photos folder> <output folder>
// The files can be replaced by hand-made / AI-made pictures of the same size: the plugin only needs the same file names, the
// same picture size (1376 x 768) and the filmstrip layout (see GuitarView.cpp).
#include <cstdio>

#include <juce_graphics/juce_graphics.h>

#include "../Source/dsp/GuitarModels.h"
#include "../Source/ui/GuitarScene.h"

using namespace fourreal::ui;

constexpr int kFrames = 48;
constexpr float kSprite = 2.0f;   // filmstrip pixels per photo pixel

static void save (const juce::Image& img, const juce::File& f, bool jpeg)
{
    f.deleteFile();
    juce::FileOutputStream out (f);
    if (jpeg)
    {
        juce::JPEGImageFormat fmt;
        fmt.setQuality (0.95f);
        fmt.writeImageToStream (img, out);
    }
    else
    {
        juce::PNGImageFormat fmt;
        fmt.writeImageToStream (img, out);
    }
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("render_art <photos folder> <output folder>\n");
        return 1;
    }
    const juce::File photos (argv[1]), outDir (argv[2]);
    outDir.createDirectory();
    for (const auto& model : fourreal::allModels())
    {
        const std::string code = model.code;
        if (code.empty())
            continue;
        const auto& scene = sceneFor (code);
        const auto photo = juce::ImageFileFormat::loadFrom (photos.getChildFile (juce::String (code) + ".jpg"));
        if (! photo.isValid())
        {
            std::printf ("missing photo %s\n", code.c_str());
            return 1;
        }
        // one picture per selector position
        for (size_t p = 0; p < model.positions.size(); ++p)
        {
            int slot = -1;
            for (int s = 0; s < scene.selector.numSlots(); ++s)
                if (scene.selector.slotMasks[static_cast<size_t> (s)] == model.positions[p].pickupMask)
                    slot = s;
            if (slot < 0)
                continue;
            juce::Image img (juce::Image::RGB, photo.getWidth(), photo.getHeight(), false);
            juce::Graphics g (img);
            g.drawImageAt (photo, 0, 0);
            for (const auto& pk : scene.pickups)
                (void) pk; // no highlight on the pickups: the selector lever shows the position
            paintSelector (g, scene.selector, static_cast<float> (slot), {}, false, juce::Colours::white);
            save (img, outDir.getChildFile (juce::String (code) + "_pos" + juce::String (static_cast<int> (p)) + ".jpg"), true);
        }
        // knob filmstrip (all knobs of a guitar share the style; the size of the first one is the reference)
        const KnobSpot ref = scene.knobs.front();
        const float half = ref.r + 12.0f;
        const int frame = juce::roundToInt (2.0f * half * kSprite);
        juce::Image strip (juce::Image::ARGB, frame, frame * kFrames, true);
        {
            juce::Graphics g (strip);
            for (int i = 0; i < kFrames; ++i)
            {
                juce::Graphics::ScopedSaveState s (g);
                g.addTransform (juce::AffineTransform::scale (kSprite).translated (0.0f, static_cast<float> (i * frame)));
                KnobSpot k = ref;
                k.x = half;
                k.y = half;
                paintKnobBody (g, k, static_cast<float> (i) / static_cast<float> (kFrames - 1));
            }
        }
        save (strip, outDir.getChildFile (juce::String (code) + "_knob.png"), false);
        std::printf ("%s ok\n", code.c_str());
    }
    return 0;
}
