// Renders the pictures the plugin shows (Source/ui/art) from the vector guitar pictures (Source/ui/photos, no knobs, no selector):
//   <CODE>_pos<i>.jpg         the guitar with the selector lever of selector position i (i = index in GuitarModel::positions)
//   <CODE>_knob_vol.png       filmstrip of the volume knob of that guitar: kFrames frames, top to bottom, knob fully down .. up
//   <CODE>_knob_tone.png      the same for the tone knob
// The knobs and levers are drawn flat from above by Source/ui/GuitarScene.cpp (positions and colours are there).
// Usage: render_art <guitar pictures folder> <output folder>
#include <cstdio>
#include <map>
#include <sstream>

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
        std::printf ("render_art <guitar pictures folder> <output folder>\n");
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI init;
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
            std::printf ("missing picture %s\n", code.c_str());
            return 1;
        }
        const auto& sel = scene.selector;
        const std::vector<bool> none;

        // one picture per selector position
        for (size_t p = 0; p < model.positions.size(); ++p)
        {
            int slot = -1;
            for (int s = 0; s < sel.numSlots(); ++s)
                if (sel.slotMasks[static_cast<size_t> (s)] == model.positions[p].pickupMask)
                    slot = s;
            if (slot < 0)
                continue;
            juce::Image img (juce::Image::RGB, photo.getWidth(), photo.getHeight(), false);
            juce::Graphics g (img);
            g.drawImageAt (photo, 0, 0);
            paintSelector (g, sel, static_cast<float> (slot), none, false, juce::Colours::white);
            save (img, outDir.getChildFile (juce::String (code) + "_pos" + juce::String (static_cast<int> (p)) + ".jpg"), true);
        }

        // knob filmstrips (one knob size per guitar: the first knob is the reference)
        KnobSpot ref = scene.knobs.front();
        const float half = ref.r + 12.0f;
        const int frame = juce::roundToInt (2.0f * half * kSprite);
        for (int kind = 0; kind < 2; ++kind)
        {
            juce::Image strip (juce::Image::ARGB, frame, frame * kFrames, true);
            juce::Graphics g (strip);
            for (int i = 0; i < kFrames; ++i)
            {
                const float v = static_cast<float> (i) / static_cast<float> (kFrames - 1);
                juce::Graphics::ScopedSaveState s (g);
                g.addTransform (juce::AffineTransform::scale (kSprite).translated (0.0f, static_cast<float> (i * frame)));
                KnobSpot k = ref;
                k.x = k.y = half;
                paintKnobBody (g, k, v);
            }
            save (strip, outDir.getChildFile (juce::String (code) + (kind == 0 ? "_knob_vol.png" : "_knob_tone.png")), false);
        }
        std::printf ("%s ok\n", code.c_str());
    }
    return 0;
}
