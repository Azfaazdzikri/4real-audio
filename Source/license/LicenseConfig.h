#pragma once

// Build-time licence settings. CMake passes them as compile definitions (see the FOURREAL_* options in CMakeLists.txt), so
// nothing secret is ever written into a source file. Only PUBLIC values go here: the server URL, the Supabase "anon"
// (publishable) key and the Ed25519 PUBLIC key. The signing (private) key stays on the server.

// 0 = test build: licensing is not enforced and every guitar is unlocked (the account panel still works if the server is
//     configured). 1 = release build: a guitar without a valid licence passes the sound through untouched.
#ifndef FOURREAL_ENFORCE_LICENSE
 #define FOURREAL_ENFORCE_LICENSE 0
#endif

// e.g. "https://abcdefgh.supabase.co" (no trailing slash)
#ifndef FOURREAL_LICENSE_API_URL
 #define FOURREAL_LICENSE_API_URL ""
#endif

// Supabase publishable ("anon") key, sent as the `apikey` header.
#ifndef FOURREAL_LICENSE_ANON_KEY
 #define FOURREAL_LICENSE_ANON_KEY ""
#endif

// Ed25519 public key, base64 or base64url, 32 bytes (printed by tools/license/gen_keys.py).
#ifndef FOURREAL_LICENSE_PUBLIC_KEY
 #define FOURREAL_LICENSE_PUBLIC_KEY ""
#endif

// Where "Forgot password" sends the player (optional).
#ifndef FOURREAL_LICENSE_ACCOUNT_URL
 #define FOURREAL_LICENSE_ACCOUNT_URL ""
#endif
