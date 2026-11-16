#pragma once

// Build-time licence settings. CMake passes them as compile definitions (see the FOURREAL_* options in CMakeLists.txt), so
// nothing secret is ever written into a source file. Only PUBLIC values go here: the server URL, the Supabase "anon"
// (publishable) key and the Ed25519 PUBLIC key. The signing (private) key stays on the server.

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

// The account site (3ffect.my.id): "Sign in with browser", "Create account" and "Forgot password" open its pages.
#ifndef FOURREAL_LICENSE_ACCOUNT_URL
 #define FOURREAL_LICENSE_ACCOUNT_URL ""
#endif
