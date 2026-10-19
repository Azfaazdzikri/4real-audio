#include "PluginEditor.h"
#include <algorithm>

namespace
{
    const juce::Colour backgroundColour (0xff15171c);

    void styleLabel (juce::Label& label, const juce::String& text, juce::Justification justification)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (justification);
        label.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    }
}

void FourRealAudioEditor::setupKnob (Knob& k, const juce::String& paramId, const juce::String& text, bool isDb)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 20);
    if (isDb)
        k.slider.setTextValueSuffix (" dB");
    k.slider.setDoubleClickReturnValue (true, isDb ? 0.0 : 10.0);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    styleLabel (k.label, text, juce::Justification::centred);

    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<Apvts::SliderAttachment> (audioProcessor.apvts, paramId, k.slider);
}

void FourRealAudioEditor::setupChoice (Choice& c, const juce::String& paramId, const juce::String& text)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.apvts.getParameter (paramId)))
        c.box.addItemList (choice->choices, 1);
    styleLabel (c.label, text, juce::Justification::centredLeft);

    addAndMakeVisible (c.box);
    addAndMakeVisible (c.label);
    c.attachment = std::make_unique<Apvts::ComboBoxAttachment> (audioProcessor.apvts, paramId, c.box);
}

void FourRealAudioEditor::setupToggle (Toggle& t, const juce::String& paramId, const juce::String& text)
{
    t.button.setButtonText (text);
    addAndMakeVisible (t.button);
    t.attachment = std::make_unique<Apvts::ButtonAttachment> (audioProcessor.apvts, paramId, t.button);
}

FourRealAudioEditor::FourRealAudioEditor (FourRealAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    const auto& target = FourRealAudioProcessor::targetModel();
    accent = juce::Colour (target.accentRgb);

    setupKnob (inputKnob, "inputGain", "INPUT", true);
    for (size_t i = 0; i < target.volumes.size(); ++i)
    {
        modelKnobs.push_back (std::make_unique<Knob>());
        setupKnob (*modelKnobs.back(), "vol" + juce::String ((int) i), target.volumes[i].label, false);
    }
    for (size_t i = 0; i < target.tones.size(); ++i)
    {
        modelKnobs.push_back (std::make_unique<Knob>());
        setupKnob (*modelKnobs.back(), "tone" + juce::String ((int) i), target.tones[i].label, false);
    }
    setupKnob (intensityKnob, "intensity", "INTENSITY", false);
    intensityKnob.slider.setTextValueSuffix (" %");
    intensityKnob.slider.setDoubleClickReturnValue (true, 100.0);
    setupKnob (outputKnob, "outputGain", "OUTPUT", true);

    styleLabel (sourceHeader,
                FourRealAudioProcessor::autoSourceAvailable() ? "MY GUITAR (SOURCE) - pick the closest recorded guitar, or Auto"
                                                              : "MY GUITAR (SOURCE) - pick the closest recorded guitar",
                juce::Justification::centredLeft);
    styleLabel (autoLabel, "", juce::Justification::centredLeft);
    autoLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (autoLabel);
    styleLabel (targetHeader, juce::String ("SOUND LIKE: ") + juce::String (target.displayName).toUpperCase(),
                juce::Justification::centredLeft);
    addAndMakeVisible (sourceHeader);
    addAndMakeVisible (targetHeader);

    setupChoice (sourceGuitar, "srcGuitar", "");
    setupChoice (sourcePosition, "srcPos", "");
    setupChoice (targetSelector, "tgtSel", "");
    setupToggle (voicingToggle, "voicing", "Voicing on");
    setupToggle (keepLevelToggle, "keepLevel", "Volume knob changes level");
    setupToggle (autoLevelToggle, "autoLevel", "Auto level (recommended)");
    refreshSourcePositions();
    targetSelector.box.setEnabled (target.positions.size() > 1); // only one recorded position: nothing to choose

    if (FourRealAudioProcessor::myGuitarAvailable())
    {
        styleLabel (slotHelp, "Pick \"My guitar\", then for each pickup position of YOUR guitar: choose which recorded guitar it is closest to, "
                              "press Learn and play a few notes with that pickup (it stops by itself after 8 s or press Stop).",
                    juce::Justification::topLeft);
        slotHelp.setFont (juce::Font (juce::FontOptions (11.5f)));
        slotHelp.setMinimumHorizontalScale (1.0f);
        addChildComponent (slotHelp);
        for (int k = 0; k < FourRealAudioProcessor::kNumSlots; ++k)
        {
            auto& col = slotCols[(size_t) k];
            styleLabel (col.title, "PICKUP " + juce::String (k + 1), juce::Justification::centredLeft);
            styleLabel (col.status, "empty", juce::Justification::centredLeft);
            col.status.setFont (juce::Font (juce::FontOptions (12.0f)));
            col.cell.addItemList ([] { juce::StringArray s; for (const auto& l : FourRealAudioProcessor::sourceCellLabels()) s.add (l); return s; }(), 1);
            col.attachment = std::make_unique<Apvts::ComboBoxAttachment> (audioProcessor.apvts, "slotCell" + juce::String (k), col.cell);
            col.learn.setButtonText ("Learn");
            col.clear.setButtonText ("Clear");
            col.learn.onClick = [this, k]
            {
                if (audioProcessor.learningSlotUi.load() == k)
                    audioProcessor.requestStopLearn();
                else
                    audioProcessor.requestLearn (k);
            };
            col.clear.onClick = [this, k] { audioProcessor.clearSlot (k); };
            addChildComponent (col.title);
            addChildComponent (col.status);
            addChildComponent (col.cell);
            addChildComponent (col.learn);
            addChildComponent (col.clear);
        }
    }

    setSize (880, 470);
    startTimerHz (30);
}

FourRealAudioEditor::~FourRealAudioEditor()
{
    stopTimer();
}

// The selector positions depend on which source guitar is chosen: only the positions that were recorded.
void FourRealAudioEditor::refreshSourcePositions()
{
    const auto& models = fourreal::allModels();
    const int guitar = juce::jlimit (0, (int) models.size(),
                                     static_cast<int> (audioProcessor.apvts.getRawParameterValue ("srcGuitar")->load() + 0.5f));
    const bool isAuto = static_cast<size_t> (guitar) >= models.size(); // the last list entry is "Auto"
    sourcePosition.box.setVisible (! isAuto);
    if (isAuto)
    {
        sourcePosition.box.clear (juce::dontSendNotification);
        shownSourceGuitar = guitar;
        return;
    }
    const auto& positions = models[static_cast<size_t> (guitar)].positions;
    const int count = (int) positions.size();

    auto* param = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.apvts.getParameter ("srcPos"));
    int index = param != nullptr ? param->getIndex() : 0;
    if (param != nullptr && index >= count)
    {
        *param = count - 1; // clamp the stored position to what this guitar has
        index = count - 1;
    }

    sourcePosition.box.clear (juce::dontSendNotification);
    for (int i = 0; i < count; ++i)
        sourcePosition.box.addItem (positions[(size_t) i].name, i + 1);
    sourcePosition.box.setSelectedId (index + 1, juce::dontSendNotification);
    shownSourceGuitar = guitar;
}

void FourRealAudioEditor::setSlotPanelVisible (bool shouldShow)
{
    if (shouldShow == slotsShown)
        return;
    slotsShown = shouldShow;
    slotHelp.setVisible (shouldShow);
    for (auto& col : slotCols)
        for (juce::Component* comp : std::initializer_list<juce::Component*> { &col.title, &col.status, &col.cell, &col.learn, &col.clear })
            comp->setVisible (shouldShow);
    setSize (880, 470 + (shouldShow ? kSlotPanelHeight : 0));
}

void FourRealAudioEditor::timerCallback()
{
    {
        const int numGuitars = (int) fourreal::allModels().size();
        const int choice = static_cast<int> (audioProcessor.apvts.getRawParameterValue ("srcGuitar")->load() + 0.5f);
        const int myIndex = numGuitars + (FourRealAudioProcessor::autoSourceAvailable() ? 1 : 0);
        setSlotPanelVisible (FourRealAudioProcessor::myGuitarAvailable() && choice == myIndex);
    }
    if (slotsShown)
    {
        const int learning = audioProcessor.learningSlotUi.load();
        for (int k = 0; k < FourRealAudioProcessor::kNumSlots; ++k)
        {
            auto& col = slotCols[(size_t) k];
            const bool valid = audioProcessor.slotValidUi[(size_t) k].load();
            const juce::String status = learning == k ? juce::String ("play now: ") + juce::String (audioProcessor.learnSecondsUi.load(), 1) + " s"
                                                      : (valid ? "learned" : "empty");
            if (col.status.getText() != status)
                col.status.setText (status, juce::dontSendNotification);
            const juce::String learnText = learning == k ? "Stop" : "Learn";
            if (col.learn.getButtonText() != learnText)
                col.learn.setButtonText (learnText);
            col.clear.setEnabled (valid && learning != k);
        }
        repaint (slotBars[0].getUnion (slotBars[FourRealAudioProcessor::kNumSlots - 1]).expanded (0, 4));
    }

    const int guitar = juce::jlimit (0, (int) fourreal::allModels().size(),
                                     static_cast<int> (audioProcessor.apvts.getRawParameterValue ("srcGuitar")->load() + 0.5f));
    if (guitar != shownSourceGuitar)
        refreshSourcePositions();

    // Automatic source: show which recorded cells the plugin thinks your guitar is closest to.
    juce::String autoText;
    if (audioProcessor.autoActiveUi.load())
    {
        std::vector<std::pair<float, int>> top;
        for (int i = 0; i < audioProcessor.numSourceCells(); ++i)
            top.emplace_back (audioProcessor.autoWeightsUi[(size_t) i].load(), i);
        std::sort (top.begin(), top.end(), [] (const auto& a, const auto& b) { return a.first > b.first; });
        autoText = "Auto hears:";
        for (size_t k = 0; k < top.size() && k < 3; ++k)
            if (top[k].first >= 0.05f)
                autoText << (k ? ",  " : " ") << audioProcessor.sourceLabel (top[k].second) << " "
                         << juce::String (juce::roundToInt (top[k].first * 100.0f)) << " %";
    }
    if (autoText != autoLabel.getText())
        autoLabel.setText (autoText, juce::dontSendNotification);

    // Quick attack, slow release.
    inputLevel  = juce::jmax (audioProcessor.inputPeak.load(),  inputLevel  * 0.85f);
    outputLevel = juce::jmax (audioProcessor.outputPeak.load(), outputLevel * 0.85f);
    repaint (meterArea);
}

void FourRealAudioEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (28.0f, juce::Font::bold)));
    const juce::String title ("4REAL AUDIO");
    g.drawText (title, 24, 14, getWidth() - 48, 32, juce::Justification::centredLeft);
    g.setColour (accent);
    g.drawText (juce::String (FourRealAudioProcessor::targetModel().code), 24 + 205, 14, 120, 32,
                juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText ("guitar tone transformer  -  v0.8.0 test build (spectral model + My guitar calibration)",
                24, 44, getWidth() - 48, 18, juce::Justification::centredLeft);

    auto drawMeter = [&g, this] (juce::Rectangle<int> r, float level, const juce::String& name)
    {
        g.setColour (juce::Colour (0xff2a2e38));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        const float norm = juce::jlimit (0.0f, 1.0f,
            juce::jmap (juce::Decibels::gainToDecibels (level, -60.0f), -60.0f, 0.0f, 0.0f, 1.0f));

        g.setColour (norm > 0.95f ? juce::Colours::red : accent);
        g.fillRoundedRectangle (r.withWidth (juce::roundToInt ((float) r.getWidth() * norm)).toFloat(), 3.0f);

        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (name, r.reduced (6, 0), juce::Justification::centredLeft);
    };

    // Hop, window and filter length are kept the same in time at every host rate, so only a failed model load is shown.
    if (! audioProcessor.modelLoaded())
    {
        g.setColour (juce::Colour (0xffffb347));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Model failed to load", 24, getHeight() - 74, getWidth() - 48, 16, juce::Justification::centredLeft);
    }

    if (slotsShown)
        for (int k = 0; k < FourRealAudioProcessor::kNumSlots; ++k)
        {
            const auto r = slotBars[(size_t) k];
            g.setColour (juce::Colour (0xff2a2e38));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            if (audioProcessor.slotValidUi[(size_t) k].load())
            {
                const float w = juce::jlimit (0.0f, 1.0f, audioProcessor.slotWeightUi[(size_t) k].load());
                g.setColour (accent);
                g.fillRoundedRectangle (r.withWidth (juce::roundToInt ((float) r.getWidth() * w)).toFloat(), 3.0f);
            }
        }

    auto m = meterArea;
    drawMeter (m.removeFromTop (18), inputLevel, "IN");
    m.removeFromTop (4);
    drawMeter (m.removeFromTop (18), outputLevel, "OUT");
}

void FourRealAudioEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    area.removeFromTop (60); // header

    meterArea = area.removeFromBottom (40);
    area.removeFromBottom (10);

    // Row of choices: source (guitar + position), target selector, toggles.
    auto top = area.removeFromTop (84);
    auto sourceArea = top.removeFromLeft (420);
    top.removeFromLeft (20);
    auto targetArea = top.removeFromLeft (210);
    top.removeFromLeft (20);
    auto toggleArea = top;

    sourceHeader.setBounds (sourceArea.removeFromTop (22));
    auto sourceRow = sourceArea.removeFromTop (28);
    sourceGuitar.box.setBounds (sourceRow.removeFromLeft (210));
    sourceRow.removeFromLeft (10);
    sourcePosition.box.setBounds (sourceRow);
    autoLabel.setBounds (sourceArea.removeFromTop (22));

    targetHeader.setBounds (targetArea.removeFromTop (22));
    targetSelector.box.setBounds (targetArea.removeFromTop (28));

    voicingToggle.button.setBounds (toggleArea.removeFromTop (26));
    keepLevelToggle.button.setBounds (toggleArea.removeFromTop (26));
    autoLevelToggle.button.setBounds (toggleArea.removeFromTop (26));

    area.removeFromTop (14);

    // "My guitar" panel (only shown when that source is chosen)
    if (slotsShown)
    {
        auto panel = area.removeFromTop (kSlotPanelHeight - 14);
        slotHelp.setBounds (panel.removeFromTop (32));
        const int gap = 8;
        const int colW = (panel.getWidth() - gap * (FourRealAudioProcessor::kNumSlots - 1)) / FourRealAudioProcessor::kNumSlots;
        for (int k = 0; k < FourRealAudioProcessor::kNumSlots; ++k)
        {
            auto col = panel.removeFromLeft (colW);
            panel.removeFromLeft (gap);
            auto& c = slotCols[(size_t) k];
            c.title.setBounds (col.removeFromTop (18));
            c.cell.setBounds (col.removeFromTop (26));
            col.removeFromTop (4);
            auto buttons = col.removeFromTop (26);
            c.learn.setBounds (buttons.removeFromLeft (colW * 6 / 10));
            buttons.removeFromLeft (4);
            c.clear.setBounds (buttons);
            c.status.setBounds (col.removeFromTop (18));
            slotBars[(size_t) k] = col.removeFromTop (8);
        }
        area.removeFromTop (6);
    }

    // Knob row: INPUT, the target guitar's volume and tone knobs, OUTPUT. Centred.
    std::vector<Knob*> knobs { &inputKnob };
    for (auto& k : modelKnobs)
        knobs.push_back (k.get());
    knobs.push_back (&intensityKnob);
    knobs.push_back (&outputKnob);

    const int colWidth = juce::jmin (130, area.getWidth() / juce::jmax (1, (int) knobs.size()));
    area.removeFromLeft ((area.getWidth() - colWidth * (int) knobs.size()) / 2);
    for (Knob* k : knobs)
    {
        auto col = area.removeFromLeft (colWidth);
        k->label.setBounds (col.removeFromTop (20));
        k->slider.setBounds (col);
    }
}
