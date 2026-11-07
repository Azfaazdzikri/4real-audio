#include "LicenseClient.h"

#include <algorithm>

#include "../License.h"
#include "Ed25519.h"

namespace fourreal
{
namespace
{
constexpr int kConnectionTimeoutMs = 10000;
constexpr int64_t kRefreshEverySeconds = 6 * 3600; // while the host keeps running

int64_t unixNow() { return juce::Time::currentTimeMillis() / 1000; }

juce::File licenseFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("4Real")
        .getChildFile ("4real-audio")
        .getChildFile ("license.json");
}

// A JSON object as a juce::var. The var owns the object, so one var can be sent any number of times.
juce::var makeObject (std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto* o = new juce::DynamicObject();
    juce::var v (o);
    for (const auto& p : properties)
        o->setProperty (p.first, p.second);
    return v;
}

juce::String trimmedSerial (const juce::String& s)
{
    return s.trim().toUpperCase().removeCharacters (" ");
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
LicenseClient::LicenseClient() : juce::Thread ("4real licence")
{
    state.configured = std::string (FOURREAL_LICENSE_API_URL).size() > 0
                       && license::decodePublicKey (FOURREAL_LICENSE_PUBLIC_KEY, publicKey);
    publicKeyOk = state.configured;
    state.enforced = LicenseManager::enforced();

    loadFile();
    startThread(); // the first renewal runs a few seconds later (see run()), so a host that only scans the plugin never connects
}

LicenseClient::~LicenseClient()
{
    signalThreadShouldExit();
    notify();
    stopThread (15000); // a request that is running ends by its own time-out
}

juce::String LicenseClient::machineId()
{
    auto raw = juce::SystemStats::getUniqueDeviceID();
    if (raw.isEmpty())
        raw = juce::SystemStats::getComputerName() + "|" + juce::SystemStats::getLogonName();
    // our own SHA-512 (license/Ed25519.h): juce::SHA256 lives in the juce_cryptography module, which this project does not link
    const auto text = (raw + "|4real-audio").toStdString();
    uint8_t digest[64];
    license::sha512 (reinterpret_cast<const uint8_t*> (text.data()), text.size(), digest);
    juce::String hex;
    for (int i = 0; i < 16; ++i)
        hex << juce::String::toHexString (digest[i]).paddedLeft ('0', 2);
    return hex; // 32 hex characters
}

juce::String LicenseClient::machineName()
{
    return juce::SystemStats::getComputerName() + " (" + juce::SystemStats::getOperatingSystemName() + ")";
}

LicenseClient::Snapshot LicenseClient::snapshot() const
{
    const juce::ScopedLock sl (lock);
    return state;
}

bool LicenseClient::isGuitarLicensed (const juce::String& code) const
{
    const juce::ScopedLock sl (lock);
    return state.licenceValid && state.guitars.contains (code);
}

void LicenseClient::post (std::function<void()> job)
{
    {
        const juce::ScopedLock sl (queueLock);
        jobs.push_back (std::move (job));
    }
    notify();
}

void LicenseClient::signIn (const juce::String& email, const juce::String& password)
{
    post ([this, email, password] { doSignIn (email, password, false); });
}

void LicenseClient::createAccount (const juce::String& email, const juce::String& password)
{
    post ([this, email, password] { doSignIn (email, password, true); });
}

void LicenseClient::redeemSerial (const juce::String& serial)
{
    post ([this, serial] { doRedeem (serial); });
}

void LicenseClient::refreshNow()
{
    post ([this] { doRefresh (false); });
}

void LicenseClient::signOut()
{
    post ([this] { doSignOut(); });
}

void LicenseClient::run()
{
    const int64_t startedAt = unixNow();
    while (! threadShouldExit())
    {
        for (;;)
        {
            std::function<void()> job;
            {
                const juce::ScopedLock sl (queueLock);
                if (jobs.empty())
                    break;
                job = std::move (jobs.front());
                jobs.pop_front();
            }
            job();
            if (threadShouldExit())
                return;
        }

        // keep the token fresh and remember that the plugin has run (guards against turning the clock back)
        bool firstPending = false;
        {
            bool due = false;
            {
                const juce::ScopedLock sl (lock);
                firstPending = state.signedIn && lastRefreshTry == 0;
                due = state.signedIn
                      && (lastRefreshTry == 0 ? unixNow() - startedAt >= 5 : unixNow() - lastRefreshTry >= kRefreshEverySeconds);
            }
            if (due)
                doRefresh (true);
        }
        wait (firstPending ? 1000 : 60 * 1000);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
juce::String LicenseClient::Response::errorText() const
{
    if (status == 0)
        return "Could not reach the licence server. Check your internet connection.";
    for (const char* key : { "message", "error_description", "msg", "error" })
        if (const auto text = json.getProperty (key, {}).toString(); text.isNotEmpty())
            return text;
    return "Server error (" + juce::String (status) + ")";
}

juce::String LicenseClient::functionUrl (const char* name) const
{
    return juce::String (FOURREAL_LICENSE_API_URL).trimCharactersAtEnd ("/") + "/functions/v1/" + name;
}

LicenseClient::Response LicenseClient::postJson (const juce::String& url, const juce::var& body, const juce::String& bearer) const
{
    Response r;
    int status = 0;
    const juce::String key (FOURREAL_LICENSE_ANON_KEY);
    const auto headers = "Content-Type: application/json\r\napikey: " + key + "\r\nAuthorization: Bearer "
                         + (bearer.isNotEmpty() ? bearer : key) + "\r\n";
    const auto stream = juce::URL (url)
                            .withPOSTData (juce::JSON::toString (body, true))
                            .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                    .withExtraHeaders (headers)
                                                    .withConnectionTimeoutMs (kConnectionTimeoutMs)
                                                    .withHttpRequestCmd ("POST")
                                                    .withStatusCode (&status));
    r.status = status;
    if (stream == nullptr)
        return r; // status 0: no connection; otherwise the server answered with an error and no body
    r.json = juce::JSON::parse (stream->readEntireStreamAsString());
    return r;
}

// ---------------------------------------------------------------------------------------------------------------------
void LicenseClient::setMessage (const juce::String& text, bool isError)
{
    {
        const juce::ScopedLock sl (lock);
        state.message = text;
        state.messageIsError = isError;
    }
    publish();
}

void LicenseClient::setBusy (bool b)
{
    {
        const juce::ScopedLock sl (lock);
        state.busy = b;
    }
    publish();
}

void LicenseClient::publish()
{
    sendChangeMessage(); // asynchronous: delivered on the message thread
}

// Checks a token with everything the plugin knows and, if it is good, makes it the current licence.
bool LicenseClient::adoptToken (const juce::String& token, juce::String& error)
{
    const int64_t now = unixNow();
    int64_t effectiveNow = now;
    {
        const juce::ScopedLock sl (lock);
        effectiveNow = juce::jmax (now, lastSeen); // turning the clock back does not extend a licence
    }
    license::Entitlement e;
    const auto status = license::verifyToken (token.toStdString(), publicKey, machineId().toStdString(), effectiveNow, e);
    if (status != license::TokenStatus::Ok)
    {
        error = license::describe (status);
        return false;
    }

    uint32_t mask = 0;
    juce::StringArray codes;
    const auto& lib = Library::get();
    for (int i = 0; i < lib.size() && i < 32; ++i)
        if (e.hasGuitar (lib.at (i).code()))
        {
            mask |= (1u << i);
            codes.add (lib.at (i).code());
        }
    {
        const juce::ScopedLock sl (lock);
        tokenText = token;
        lastSeen = effectiveNow;
        state.signedIn = true;
        state.licenceValid = true;
        state.email = juce::String::fromUTF8 (e.email.c_str());
        state.expiresAt = e.expiresAt;
        state.guitars = codes;
    }
    LicenseManager::setUnlockedMask (mask);
    return true;
}

void LicenseClient::clearLicence (const juce::String& why, bool isError)
{
    {
        const juce::ScopedLock sl (lock);
        tokenText.clear();
        deviceId.clear();
        deviceSecret.clear();
        state.signedIn = false;
        state.licenceValid = false;
        state.email.clear();
        state.expiresAt = 0;
        state.guitars.clear();
    }
    LicenseManager::setUnlockedMask (0);
    saveFile();
    setMessage (why, isError);
}

void LicenseClient::loadFile()
{
    const auto f = licenseFile();
    if (! f.existsAsFile())
        return;
    const auto json = juce::JSON::parse (f.loadFileAsString());
    if (! json.isObject())
        return;

    {
        const juce::ScopedLock sl (lock);
        deviceId = json.getProperty ("deviceId", {}).toString();
        deviceSecret = json.getProperty ("deviceSecret", {}).toString();
        tokenText = json.getProperty ("token", {}).toString();
        lastSeen = static_cast<int64_t> (json.getProperty ("lastSeen", 0));
        state.signedIn = deviceId.isNotEmpty() && deviceSecret.isNotEmpty();
    }
    if (! publicKeyOk)
        return;
    juce::String error;
    const auto token = [this] { const juce::ScopedLock sl (lock); return tokenText; }();
    if (token.isNotEmpty() && ! adoptToken (token, error))
    {
        const juce::ScopedLock sl (lock);
        state.message = error; // keep the device: a refresh may fix an expired token
        state.messageIsError = true;
    }
}

void LicenseClient::saveFile()
{
    juce::var o;
    {
        const juce::ScopedLock sl (lock);
        o = makeObject ({ { "deviceId", deviceId },
                          { "deviceSecret", deviceSecret },
                          { "token", tokenText },
                          { "lastSeen", static_cast<juce::int64> (lastSeen) } });
    }
    const auto f = licenseFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (o, false));
}

// ---------------------------------------------------------------------------------------------------------------------
void LicenseClient::doSignIn (const juce::String& email, const juce::String& password, bool create)
{
    if (! publicKeyOk)
    {
        setMessage ("This build has no licence server configured.", true);
        return;
    }
    if (email.trim().isEmpty() || password.isEmpty())
    {
        setMessage ("Enter your email and password.", true);
        return;
    }
    setBusy (true);
    struct Done { LicenseClient& c; ~Done() { c.setBusy (false); } } done { *this };

    const auto api = juce::String (FOURREAL_LICENSE_API_URL).trimCharactersAtEnd ("/");
    const auto body = makeObject ({ { "email", email.trim() }, { "password", password } });

    if (create)
    {
        const auto r = postJson (api + "/auth/v1/signup", body, {});
        if (r.status < 200 || r.status >= 300)
        {
            setMessage (r.errorText(), true);
            return;
        }
        if (r.json.getProperty ("access_token", {}).toString().isEmpty())
        {
            setMessage ("Account created. Check your email to confirm it, then sign in.", false);
            return;
        }
    }

    // 1. sign in with the account (the access token only lives in this function)
    const auto login = postJson (api + "/auth/v1/token?grant_type=password", body, {});
    const auto access = login.json.getProperty ("access_token", {}).toString();
    if (login.status != 200 || access.isEmpty())
    {
        setMessage (login.errorText(), true);
        return;
    }

    // 2. register this computer
    const auto reg = postJson (functionUrl ("device-login"),
                               makeObject ({ { "machineId", machineId() }, { "machineName", machineName() } }), access);
    const auto token = reg.json.getProperty ("token", {}).toString();
    const auto newId = reg.json.getProperty ("deviceId", {}).toString();
    const auto newSecret = reg.json.getProperty ("deviceSecret", {}).toString();
    if (reg.status != 200 || token.isEmpty() || newId.isEmpty() || newSecret.isEmpty())
    {
        setMessage (reg.errorText(), true);
        return;
    }

    juce::String error;
    if (! adoptToken (token, error))
    {
        setMessage (error, true);
        return;
    }
    {
        const juce::ScopedLock sl (lock);
        deviceId = newId;
        deviceSecret = newSecret;
        state.signedIn = true;
        lastRefreshTry = unixNow();
    }
    saveFile();
    setMessage ("Signed in.", false);
}

void LicenseClient::doRedeem (const juce::String& serialText)
{
    const auto serial = trimmedSerial (serialText);
    if (serial.isEmpty())
    {
        setMessage ("Enter the serial number of the guitar.", true);
        return;
    }
    juce::String id, secret;
    {
        const juce::ScopedLock sl (lock);
        id = deviceId;
        secret = deviceSecret;
    }
    if (id.isEmpty())
    {
        setMessage ("Sign in first.", true);
        return;
    }
    setBusy (true);
    struct Done { LicenseClient& c; ~Done() { c.setBusy (false); } } done { *this };

    const auto r = postJson (functionUrl ("redeem-key"),
                             makeObject ({ { "deviceId", id }, { "deviceSecret", secret }, { "machineId", machineId() }, { "serial", serial } }), {});
    const auto token = r.json.getProperty ("token", {}).toString();
    if (r.status != 200 || token.isEmpty())
    {
        setMessage (r.errorText(), true);
        return;
    }
    juce::String error;
    if (! adoptToken (token, error))
    {
        setMessage (error, true);
        return;
    }
    saveFile();
    setMessage ("Serial accepted. The guitar is activated.", false);
}

void LicenseClient::doRefresh (bool silent)
{
    juce::String id, secret;
    {
        const juce::ScopedLock sl (lock);
        id = deviceId;
        secret = deviceSecret;
        lastRefreshTry = unixNow();
    }
    if (id.isEmpty() || ! publicKeyOk)
        return;
    if (! silent)
        setBusy (true);
    struct Done { LicenseClient& c; bool silent; ~Done() { if (! silent) c.setBusy (false); } } done { *this, silent };

    const auto r = postJson (functionUrl ("device-refresh"),
                             makeObject ({ { "deviceId", id }, { "deviceSecret", secret }, { "machineId", machineId() } }), {});

    if (r.status == 0)
    {
        // offline: the stored token keeps working until it runs out
        if (! silent)
            setMessage (r.errorText(), true);
        return;
    }
    // Only an explicit answer from our server signs the computer out. A wrong URL, a gateway error or an outage must never
    // lock the player's guitars.
    if (r.json.getProperty ("error", {}).toString() == "device_revoked")
    {
        clearLicence ("This computer is no longer signed in to your account. Please sign in again.", true);
        return;
    }
    const auto token = r.json.getProperty ("token", {}).toString();
    if (r.status != 200 || token.isEmpty())
    {
        if (! silent)
            setMessage (r.errorText(), true);
        return;
    }
    juce::String error;
    if (! adoptToken (token, error))
    {
        setMessage (error, true);
        return;
    }
    saveFile();
    if (! silent)
        setMessage ("Licence updated.", false);
    else
        publish();
}

void LicenseClient::doSignOut()
{
    juce::String id, secret;
    {
        const juce::ScopedLock sl (lock);
        id = deviceId;
        secret = deviceSecret;
    }
    if (id.isNotEmpty() && publicKeyOk)
    {
        // Frees the computer slot on the server. If we are offline the slot stays used until it is removed online.
        postJson (functionUrl ("device-logout"), makeObject ({ { "deviceId", id }, { "deviceSecret", secret } }), {});
    }
    clearLicence ("Signed out.", false);
}
} // namespace fourreal
