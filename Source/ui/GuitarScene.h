#pragma once

#include <string>
#include <vector>

#include <juce_graphics/juce_graphics.h>

// What is drawn on top of the photo of a library guitar, and where: the pickups (they light up when selected), the volume and
// tone knobs (a value ring and a pointer on top of the real knob in the photo), and the pickup selector lever (the lever of
// the photo is erased by tools/prepare_guitar_photos.py, the plugin draws its own so that it can move).
//
// Everything here is in PHOTO coordinates (1376 x 768 pixels, the size of the photos in Source/ui/photos); the caller sets a
// transform that scales the photo to the screen. Only juce_graphics is needed, no components, so the painting can also be
// rendered by the headless preview tool (tools/ui_preview.cpp).
//
// To fix the position of something on a guitar, change the numbers in GuitarScene.cpp and look at the result with the preview
// tool; the numbers are pixels in the photo.
namespace fourreal::ui
{
constexpr float kPhotoW = 1376.0f;
constexpr float kPhotoH = 768.0f;

struct PickupSpot
{
    juce::Rectangle<float> box; // the pickup cover in the photo
    int pickup;                 // index in GuitarModel::pickups (0 = bridge)
};

enum class KnobKind { Volume, Tone };

struct KnobSpot
{
    float x, y, r;   // centre and radius of the knob cap in the photo
    KnobKind kind;
    int index;       // index in GuitarModel::volumes or GuitarModel::tones
};

enum class LeverStyle
{
    Toggle, // a lever on a pivot that tilts to the bridge side, the middle, or the neck side
    Blade   // a lever that slides along a slot (Stratocaster, Telecaster, PRS style)
};

struct SelectorSpot
{
    LeverStyle style = LeverStyle::Toggle;
    juce::Point<float> pivot;          // Toggle: where the lever is mounted. Blade: the slot point of position 0 (bridge end)
    juce::Point<float> slotEnd;        // Blade only: the slot point of the last position (neck end)
    juce::Point<float> direction;      // Toggle: unit vector of the lever at the middle position.
                                       // Blade: unit vector from the slot point to the end of the lever
    float tiltDeg = 35.0f;             // Toggle: tilt at the bridge (-) and neck (+) positions
    float length = 15.0f, width = 12.0f;
    std::vector<unsigned> slotMasks;   // pickup mask (bit i = pickup i) that every physical position of the real switch selects,
                                       // from the bridge end to the neck end
    juce::Colour tip, tipShade;
    float nutRadius = 0.0f;            // Toggle: radius of the mounting ring the plugin draws under the lever (0 = the ring of the
    juce::Colour nutLight, nutDark;    //         photo is intact and stays)
    bool drawSlot = false;             // Blade: draw the slot (when the photo's own slot was damaged by erasing the lever)

    int numSlots() const { return static_cast<int> (slotMasks.size()); }
    juce::Point<float> slotPoint (float slot) const;       // Blade: where the lever starts for a (fractional) slot
    juce::Rectangle<float> hitBox() const;                 // where a click moves the lever
    int slotAt (juce::Point<float> p) const;               // nearest slot to a point (for clicking and dragging)
};

struct GuitarScene
{
    std::string code;
    std::vector<PickupSpot> pickups;
    std::vector<KnobSpot> knobs;
    SelectorSpot selector;
};

const GuitarScene& sceneFor (const std::string& code);

// --- painting (photo coordinates) ---------------------------------------------------------------------------------------
void paintPickup (juce::Graphics&, const PickupSpot&, bool selected, bool hovered, juce::Colour accent);
// value01: knob position 0 (fully down) .. 1 (fully up).
void paintKnob (juce::Graphics&, const KnobSpot&, float value01, bool hovered, bool dragging, juce::Colour accent);
// slot: fractional so that the lever can glide between positions. available[i]: slot i was recorded and can be chosen.
void paintSelector (juce::Graphics&, const SelectorSpot&, float slot, const std::vector<bool>& available, bool hovered,
                    juce::Colour accent);
// A small label with an arrow-less pill, above (or below) the anchor; `pixel` is the size of one screen pixel in photo units.
void paintTag (juce::Graphics&, juce::Point<float> anchor, const juce::String& title, const juce::String& value, float pixel,
               juce::Colour accent);
} // namespace fourreal::ui
