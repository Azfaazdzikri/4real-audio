#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

namespace fourreal::ui
{
// The account window: sign in / create account, the list of guitars with ACTIVE or LOCKED, serial number entry (activates a
// guitar), refresh and sign out. It is an overlay inside the plugin editor (no separate window, no modal loop).
class AccountPanel : public juce::Component, private juce::ChangeListener
{
public:
    explicit AccountPanel (FourRealAudioProcessor&);
    ~AccountPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override;

    std::function<void()> onClose;
    void setHighlightedGuitar (int index) { highlighted = index; repaint(); } // the locked guitar the player clicked, or -1

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void update();
    juce::Rectangle<int> cardBounds() const;

    FourRealAudioProcessor& processor;
    fourreal::LicenseClient& client;

    juce::Label title, info, status, serialLabel;
    juce::TextEditor email, password, serial;
    juce::TextButton signInButton { "Sign in" }, createButton { "Create account" }, signOutButton { "Sign out" },
        refreshButton { "Refresh" }, redeemButton { "Activate serial" }, closeButton { "Close" }, forgotButton { "Forgot password" };

    juce::Rectangle<int> rowsArea;
    int highlighted = -1;
    bool shownSignedIn = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccountPanel)
};
}
