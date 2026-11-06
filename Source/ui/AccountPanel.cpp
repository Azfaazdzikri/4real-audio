#include "AccountPanel.h"

namespace fourreal::ui
{
namespace
{
constexpr int kCardW = 560, kCardH = 500;
const juce::Colour cardColour (0xff1d2027), lineColour (0xff2f3440), okColour (0xff58c47c), lockedColour (0xffe0a458),
    errorColour (0xffff7a7a);
}

AccountPanel::AccountPanel (FourRealAudioProcessor& p) : processor (p), client (p.license.get())
{
    title.setText ("4REAL AUDIO ACCOUNT", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, juce::Colours::white);
    info.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    info.setFont (juce::Font (juce::FontOptions (12.5f)));
    status.setFont (juce::Font (juce::FontOptions (13.0f)));
    status.setJustificationType (juce::Justification::topLeft);
    serialLabel.setText ("Serial number of a guitar:", juce::dontSendNotification);
    serialLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));

    email.setTextToShowWhenEmpty ("Email", juce::Colours::grey);
    password.setTextToShowWhenEmpty ("Password", juce::Colours::grey);
    password.setPasswordCharacter (juce::juce_wchar (0x2022));
    serial.setTextToShowWhenEmpty ("4RL-LP-XXXXX-XXXXX-XXXXX", juce::Colours::grey);
    for (auto* e : { &email, &password, &serial })
    {
        e->setMultiLine (false);
        e->setReturnKeyStartsNewLine (false);
    }
    email.setInputRestrictions (200);
    password.setInputRestrictions (200);
    serial.setInputRestrictions (64);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &title, &info, &status, &serialLabel, &email, &password, &serial, &signInButton, &createButton, &signOutButton,
             &refreshButton, &redeemButton, &closeButton, &forgotButton })
        addAndMakeVisible (c);

    signInButton.onClick = [this] { client.signIn (email.getText(), password.getText()); };
    createButton.onClick = [this] { client.createAccount (email.getText(), password.getText()); };
    password.onReturnKey = [this] { client.signIn (email.getText(), password.getText()); };
    signOutButton.onClick = [this] { client.signOut(); };
    refreshButton.onClick = [this] { client.refreshNow(); };
    redeemButton.onClick = [this] { client.redeemSerial (serial.getText()); serial.clear(); };
    serial.onReturnKey = [this] { client.redeemSerial (serial.getText()); serial.clear(); };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    forgotButton.onClick = [this]
    {
        const juce::String url (FOURREAL_LICENSE_ACCOUNT_URL);
        if (url.isNotEmpty())
            juce::URL (url).launchInDefaultBrowser();
    };
    forgotButton.setVisible (juce::String (FOURREAL_LICENSE_ACCOUNT_URL).isNotEmpty());

    client.addChangeListener (this);
    update();
}

AccountPanel::~AccountPanel()
{
    client.removeChangeListener (this);
}

void AccountPanel::visibilityChanged()
{
    if (isVisible())
    {
        update();
        client.refreshNow(); // the list of licensed guitars is current when the player looks at it
    }
}

void AccountPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    update();
}

juce::Rectangle<int> AccountPanel::cardBounds() const
{
    return juce::Rectangle<int> (kCardW, kCardH).withCentre (getLocalBounds().getCentre());
}

void AccountPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()) && onClose)
        onClose();
}

void AccountPanel::update()
{
    const auto s = client.snapshot();
    shownSignedIn = s.signedIn;

    const bool configured = s.configured;
    for (auto* c : { &email, &password })
        c->setVisible (! s.signedIn && configured);
    signInButton.setVisible (! s.signedIn && configured);
    createButton.setVisible (! s.signedIn && configured);
    signOutButton.setVisible (s.signedIn);
    refreshButton.setVisible (s.signedIn);
    serialLabel.setVisible (s.signedIn);
    serial.setVisible (s.signedIn);
    redeemButton.setVisible (s.signedIn);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &signInButton, &createButton, &signOutButton, &refreshButton, &redeemButton })
        c->setEnabled (! s.busy);

    juce::String line;
    if (! configured)
        line = "This build has no licence server configured.";
    else if (s.signedIn)
        line = "Signed in as " + s.email + (s.licenceValid ? "   -   licence valid until " + juce::Time (s.expiresAt * 1000).toString (true, false) : "");
    else
        line = "Sign in to see and activate your guitars.";
    info.setText (line, juce::dontSendNotification);

    juce::String text = s.message;
    if (s.busy)
        text = "Please wait...";
    else if (! s.enforced && text.isEmpty())
        text = "Test build: licensing is not enforced, every guitar is unlocked.";
    status.setColour (juce::Label::textColourId, s.messageIsError && ! s.busy ? errorColour : juce::Colours::white.withAlpha (0.8f));
    status.setText (text, juce::dontSendNotification);

    resized();
    repaint();
}

void AccountPanel::resized()
{
    auto card = cardBounds().reduced (24, 20);
    title.setBounds (card.removeFromTop (28));
    info.setBounds (card.removeFromTop (22));
    card.removeFromTop (8);

    if (! shownSignedIn)
    {
        email.setBounds (card.removeFromTop (30));
        card.removeFromTop (8);
        password.setBounds (card.removeFromTop (30));
        card.removeFromTop (10);
        auto row = card.removeFromTop (30);
        signInButton.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (8);
        createButton.setBounds (row.removeFromLeft (140));
        row.removeFromLeft (8);
        forgotButton.setBounds (row.removeFromLeft (140));
        card.removeFromTop (10);
    }
    else
    {
        auto row = card.removeFromTop (30);
        refreshButton.setBounds (row.removeFromLeft (110));
        row.removeFromLeft (8);
        signOutButton.setBounds (row.removeFromLeft (110));
        card.removeFromTop (10);
    }

    // guitars list (always shown, so a signed-out player sees what exists)
    const int rowH = 28;
    rowsArea = card.removeFromTop (rowH * juce::jmax (1, processor.numGuitars()));
    card.removeFromTop (10);

    if (shownSignedIn)
    {
        serialLabel.setBounds (card.removeFromTop (22));
        auto row = card.removeFromTop (30);
        redeemButton.setBounds (row.removeFromRight (130));
        row.removeFromRight (8);
        serial.setBounds (row);
        card.removeFromTop (10);
    }
    status.setBounds (card.removeFromTop (juce::jmax (20, card.getHeight() - 40)));
    closeButton.setBounds (card.removeFromRight (100).withHeight (30));
}

void AccountPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xcc000000));
    const auto card = cardBounds();
    g.setColour (cardColour);
    g.fillRoundedRectangle (card.toFloat(), 12.0f);
    g.setColour (lineColour);
    g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 12.0f, 1.0f);

    const auto s = client.snapshot();
    const int rowH = 28;
    for (int i = 0; i < processor.numGuitars(); ++i)
    {
        const auto& entry = FourRealAudioProcessor::library().at (i);
        const auto r = juce::Rectangle<int> (rowsArea.getX(), rowsArea.getY() + i * rowH, rowsArea.getWidth(), rowH);
        const bool active = ! s.enforced || (s.licenceValid && s.guitars.contains (entry.code()));

        if (i == highlighted)
        {
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (r.toFloat().reduced (0.0f, 1.0f), 5.0f);
        }
        g.setColour (juce::Colour (0xff000000u | entry.guitar->accentRgb));
        g.fillEllipse (static_cast<float> (r.getX() + 8), static_cast<float> (r.getCentreY()) - 4.0f, 8.0f, 8.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawText (entry.displayName(), r.withTrimmedLeft (26), juce::Justification::centredLeft, true);
        g.setColour (active ? okColour : lockedColour);
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        g.drawText (active ? (s.enforced ? "ACTIVE" : "UNLOCKED (test build)") : "LOCKED", r.withTrimmedRight (8), juce::Justification::centredRight, false);
        g.setColour (lineColour);
        g.drawHorizontalLine (r.getBottom() - 1, static_cast<float> (r.getX()), static_cast<float> (r.getRight()));
    }
}
}
