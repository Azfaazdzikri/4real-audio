#pragma once

#include <deque>
#include <functional>

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

#include "Token.h"

// The plugin's side of the licence system: signs the player in, registers this computer, keeps the signed licence token
// renewed and tells LicenseManager which guitars are unlocked. One instance per process, shared by every plugin instance
// (juce::SharedResourcePointer). All network calls run on its own worker thread; the audio thread is never involved
// (it only reads LicenseManager's bit mask).
//
// What is stored on disk (<user app data>/4Real/4real-audio/license.json): the device id, the device secret, the last token
// and the last time the plugin ran. The password is NEVER stored: it is sent once to sign in, then the server hands out a
// device secret that can only renew the licence of this one computer and can be revoked on the server.
namespace fourreal
{
class LicenseClient : public juce::ChangeBroadcaster, private juce::Thread
{
public:
    LicenseClient();
    ~LicenseClient() override;

    struct Snapshot
    {
        bool configured = false;  // server URL and public key are present in this build
        bool enforced = false;    // FOURREAL_ENFORCE_LICENSE
        bool signedIn = false;
        bool busy = false;        // a request is running
        bool licenceValid = false; // a token that passed every check is loaded
        juce::String email;
        juce::String message;     // last result, for the panel
        bool messageIsError = false;
        int64_t expiresAt = 0;    // unix seconds, 0 when there is no valid token
        juce::StringArray guitars; // codes of the licensed guitars
    };
    Snapshot snapshot() const;
    bool isGuitarLicensed (const juce::String& code) const;

    // Asynchronous; the outcome appears in snapshot() and a change message is sent (message thread).
    void signIn (const juce::String& email, const juce::String& password);
    void createAccount (const juce::String& email, const juce::String& password);
    void redeemSerial (const juce::String& serial);
    void refreshNow();
    void signOut();

    // The player's own computer, as the server sees it (also shown in the panel).
    static juce::String machineId();
    static juce::String machineName();

private:
    void run() override;
    void post (std::function<void()> job);

    // --- worker thread -----------------------------------------------------------------------------------------------
    struct Response
    {
        int status = 0;            // HTTP status, 0 = no connection
        juce::var json;
        juce::String errorText() const; // best human readable message in an error response
    };
    Response postJson (const juce::String& url, const juce::var& body, const juce::String& bearer) const;
    juce::String functionUrl (const char* name) const;

    void doSignIn (const juce::String& email, const juce::String& password, bool create);
    void doRedeem (const juce::String& serial);
    void doRefresh (bool silent);
    void doSignOut();
    bool adoptToken (const juce::String& token, juce::String& error);
    void clearLicence (const juce::String& why, bool isError);
    void loadFile();
    void saveFile();
    void setMessage (const juce::String& text, bool isError);
    void setBusy (bool b);
    void publish();

    mutable juce::CriticalSection lock;
    Snapshot state;
    juce::String deviceId, deviceSecret, tokenText;
    int64_t lastSeen = 0;
    int64_t lastRefreshTry = 0;
    uint8_t publicKey[32] = {};
    bool publicKeyOk = false;

    juce::CriticalSection queueLock;
    std::deque<std::function<void()>> jobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicenseClient)
};
} // namespace fourreal
