// Renders the pictures the plugin shows (Source/ui/art) from the clean guitar photos (Source/ui/photos) and the knob / selector
// parts (Source/ui/parts, cut out by tools/prepare_parts.py):
//   <CODE>_pos<i>.jpg         the guitar with the selector lever of selector position i (i = index in GuitarModel::positions)
//   <CODE>_knob_vol.png       filmstrip of the volume knob of that guitar: kFrames frames, top to bottom, knob fully down .. up
//   <CODE>_knob_tone.png      the same for the tone knob
// Usage: render_art <photos folder> <parts folder> <output folder>
// Where things are comes from Source/ui/GuitarScene.cpp. The files can be replaced by hand-made pictures of the same size
// (1376 x 768 for the guitar, the same filmstrip layout for the knobs, see GuitarView.cpp).
#include <cstdio>
#include <map>
#include <sstream>

#include <juce_graphics/juce_graphics.h>

#include "../Source/dsp/GuitarModels.h"
#include "../Source/ui/GuitarScene.h"

using namespace fourreal::ui;

constexpr int kFrames = 48;
constexpr float kSprite = 2.0f;   // filmstrip pixels per photo pixel

struct Sprite
{
    juce::Image img;
    float ax = 0, ay = 0;
};

static std::map<std::string, Sprite> gParts;

static void loadParts (const juce::File& dir)
{
    std::istringstream in (dir.getChildFile ("anchors.txt").loadFileAsString().toStdString());
    std::string name;
    float ax, ay;
    int w, h;
    while (in >> name >> ax >> ay >> w >> h)
    {
        Sprite s;
        s.img = juce::ImageFileFormat::loadFrom (dir.getChildFile (juce::String (name) + ".png"));
        s.ax = ax;
        s.ay = ay;
        gParts[name] = s;
    }
}

static const Sprite& part (const std::string& name)
{
    static const Sprite none;
    auto it = gParts.find (name);
    if (it == gParts.end() || ! it->second.img.isValid())
    {
        std::printf ("missing part %s\n", name.c_str());
        std::exit (3);
    }
    return it->second;
}

// Scales an image down by repeated halving (a plain box filter each time), then to the exact size: sharp and without aliasing.
static juce::Image scaleBy (juce::Image img, float scale)
{
    float s = scale;
    while (s < 0.5f)
    {
        img = img.rescaled (juce::jmax (1, img.getWidth() / 2), juce::jmax (1, img.getHeight() / 2), juce::Graphics::highResamplingQuality);
        s *= 2.0f;
    }
    return img.rescaled (juce::jmax (1, juce::roundToInt (static_cast<float> (img.getWidth()) * s)),
                         juce::jmax (1, juce::roundToInt (static_cast<float> (img.getHeight()) * s)), juce::Graphics::highResamplingQuality);
}

// Draws a part so that its anchor lies on (x, y) (units of g), scaled and rotated about the anchor, with a soft drop shadow.
static void drawPart (juce::Graphics& g, const Sprite& s, float x, float y, float scale, float rotRad, float unit, bool shadow = true)
{
    const float total = scale * unit;
    const auto pre = scaleBy (s.img, total);
    const float ax = s.ax * total, ay = s.ay * total;
    const auto xf = juce::AffineTransform::translation (-ax, -ay).rotated (rotRad).translated (x * unit, y * unit);
    if (shadow)
    {
        juce::Image sh (juce::Image::ARGB, pre.getWidth() + 16, pre.getHeight() + 16, true);
        for (int yy = 0; yy < pre.getHeight(); ++yy)
            for (int xx = 0; xx < pre.getWidth(); ++xx)
                sh.setPixelAt (xx + 8, yy + 8, juce::Colour::fromFloatRGBA (0, 0, 0, pre.getPixelAt (xx, yy).getFloatAlpha()));
        juce::ImageConvolutionKernel k (7);
        k.createGaussianBlur (1.6f * unit);
        k.applyToImage (sh, sh, sh.getBounds());
        g.setOpacity (0.45f);
        g.drawImageTransformed (sh, juce::AffineTransform::translation (-ax - 8, -ay - 8).rotated (rotRad)
                                        .translated (x * unit + 1.4f * unit, y * unit + 2.4f * unit));
    }
    g.setOpacity (1.0f);
    g.drawImageTransformed (pre, xf);
}

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

namespace
{
enum class Sel { Toggle3, ToggleRot, Blade3, Blade5 };

struct Look
{
    const char* knobVol;
    const char* knobTone;
    bool pointerOnKnob;      // the knob picture has no mark that turns: draw a pointer
    Sel sel;
    const char* pose[5];     // selector pictures by slot (Blade5: slot 0..4, Blade3/Toggle3: 0..2)
    float selScale;          // selector picture scale; Toggle: derived from the nut radius when 0
    float nutPx;             // Toggle: diameter of the mounting nut in the picture
    float rotDeg;            // Blade: rotation of the picture
};

const std::map<std::string, Look>& looks()
{
    static const std::map<std::string, Look> m = {
        { "LP",  { "gib_vol", "gib_tone", false, Sel::Toggle3, { "gib_tog_l", "gib_tog_c", "gib_tog_r" }, 0, 100, 0 } },
        { "335", { "gib_vol", "gib_tone", false, Sel::Toggle3, { "gib_tog_l", "gib_tog_c", "gib_tog_r" }, 0, 100, 0 } },
        { "SG",  { "gib_vol", "gib_tone", false, Sel::Toggle3, { "gib_tog_l", "gib_tog_c", "gib_tog_r" }, 0, 100, 0 } },
        { "FLC", { "amb_vol", "amb_tone", false, Sel::Toggle3, { "gib_tog_l", "gib_tog_c", "gib_tog_r" }, 0, 100, 0 } },
        { "RIC", { "ric_vol", "ric_tone", false, Sel::ToggleRot, { "ric_tog" }, 0, 160, 0 } },
        { "PRS", { "amb_vol", "amb_tone", false, Sel::Blade3, { "prs_l", "prs_c", "prs_r" }, 0.17f, 0, 0 } },
        { "STR", { "crm_vol", "crm_tone", false, Sel::Blade5, { "str_1", "str_2", "str_3", "str_4", "str_5" }, 0.24f, 0, -90 } },
        { "TCR", { "chr_vol", "chr_tone", true, Sel::Blade3, { "tele_l", "tele_c", "tele_r" }, 0.24f, 0, 0 } },
    };
    return m;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf ("render_art <photos folder> <parts folder> <output folder>\n");
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File photos (argv[1]), partsDir (argv[2]), outDir (argv[3]);
    outDir.createDirectory();
    loadParts (partsDir);

    for (const auto& model : fourreal::allModels())
    {
        const std::string code = model.code;
        if (code.empty())
            continue;
        const auto& scene = sceneFor (code);
        const auto& look = looks().at (code);
        const auto photo = juce::ImageFileFormat::loadFrom (photos.getChildFile (juce::String (code) + ".jpg"));
        if (! photo.isValid())
        {
            std::printf ("missing photo %s\n", code.c_str());
            return 1;
        }
        const auto& sel = scene.selector;

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
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            switch (look.sel)
            {
                case Sel::Toggle3:
                {
                    const float scale = 2.0f * sel.nutRadius / look.nutPx;
                    drawPart (g, part (look.pose[slot]), sel.pivot.x, sel.pivot.y, scale, 0.0f, 1.0f);
                    break;
                }
                case Sel::ToggleRot:
                {
                    const float scale = 2.0f * sel.nutRadius / look.nutPx;
                    const float deg[3] = { 58.0f, 29.0f, 0.0f };   // the picture shows the lever at the neck side
                    drawPart (g, part (look.pose[0]), sel.pivot.x, sel.pivot.y, scale, juce::degreesToRadians (deg[slot]), 1.0f);
                    break;
                }
                case Sel::Blade3:
                case Sel::Blade5:
                {
                    const auto c = (sel.pivot + sel.slotEnd) * 0.5f;
                    drawPart (g, part (look.pose[slot]), c.x, c.y, look.selScale, juce::degreesToRadians (look.rotDeg), 1.0f);
                    break;
                }
            }
            save (img, outDir.getChildFile (juce::String (code) + "_pos" + juce::String (static_cast<int> (p)) + ".jpg"), true);
        }

        // knob filmstrips (one knob size per guitar: the first knob is the reference)
        const KnobSpot ref = scene.knobs.front();
        const float half = ref.r + 12.0f;
        const int frame = juce::roundToInt (2.0f * half * kSprite);
        for (int kind = 0; kind < 2; ++kind)
        {
            const auto& sp = part (kind == 0 ? look.knobVol : look.knobTone);
            const float diameter = static_cast<float> (juce::jmax (sp.img.getWidth(), sp.img.getHeight()));
            const float scale = 2.0f * (ref.r + 2.5f) / (look.pointerOnKnob ? diameter * 0.92f : diameter);
            juce::Image strip (juce::Image::ARGB, frame, frame * kFrames, true);
            juce::Graphics g (strip);
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            for (int i = 0; i < kFrames; ++i)
            {
                const float v = static_cast<float> (i) / static_cast<float> (kFrames - 1);
                const float a = (v - 0.5f) * 1.5f * juce::MathConstants<float>::pi;   // -135 .. +135 degrees
                juce::Graphics::ScopedSaveState s (g);
                g.addTransform (juce::AffineTransform::translation (0.0f, static_cast<float> (i * frame)));
                drawPart (g, sp, half, half, scale, look.pointerOnKnob ? 0.0f : a, kSprite);
                if (look.pointerOnKnob)
                {
                    // the knob has no mark of its own: a pointer line on the top face (it turns, the knob picture does not)
                    const juce::Point<float> c (half * kSprite, half * kSprite), d (std::sin (a), -std::cos (a));
                    const float R = (ref.r + 2.5f) * kSprite;
                    const juce::Line<float> line (c + d * (R * 0.18f), c + d * (R * 0.72f));
                    g.setColour (juce::Colours::black.withAlpha (0.3f));
                    g.drawLine (juce::Line<float> (line.getStart() + juce::Point<float> (0.8f, 1.2f), line.getEnd() + juce::Point<float> (0.8f, 1.2f)), 2.6f * kSprite * 0.5f);
                    g.setColour (juce::Colour (0xff2a2c31));
                    g.drawLine (line, 2.0f * kSprite * 0.5f);
                }
            }
            save (strip, outDir.getChildFile (juce::String (code) + (kind == 0 ? "_knob_vol.png" : "_knob_tone.png")), false);
        }
        std::printf ("%s ok\n", code.c_str());
    }
    return 0;
}
