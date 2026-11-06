# Licence server (Supabase)

Accounts, serial numbers and per-guitar activation for 4real-audio. The plugin signs in with an account, registers the computer,
and receives a **signed token** listing the guitars that account owns. Guitars not in the token stay LOCKED.

```
plugin --(email+password)--> Supabase Auth ----------------> access token
plugin --(access token)----> device-login   --> deviceId + deviceSecret + signed token
plugin --(deviceSecret)----> device-refresh --> fresh token (every 6 h while online; token lasts 30 days offline)
plugin --(deviceSecret)----> redeem-key     --> serial unlocks guitar(s), new token
plugin --(deviceSecret)----> device-logout  --> frees the computer slot
```

The password is never stored in the plugin. The token is signed with an Ed25519 private key that only the server has; the plugin
contains only the public key, so it verifies offline but cannot forge a licence.

## One-time setup

1. Create a Supabase project. In Authentication, decide whether email confirmation is required.
2. Make the key pair (keep the private file secret, never commit it, keep one offline backup):
   `python3 tools/license/gen_keys.py license_private_key.txt`  (prints the PUBLIC key)
3. `supabase link --project-ref <ref>`, then `supabase db push` (applies `server/supabase/migrations`).
4. `supabase secrets set LICENSE_SIGNING_KEY="$(cat license_private_key.txt)"`
   Optional: `LICENSE_MAX_DEVICES` (default 3), `LICENSE_TOKEN_DAYS` (default 30).
5. `supabase functions deploy device-login device-refresh device-logout redeem-key` (run from `server/`).
6. Release build of the plugin:
   `cmake -DFOURREAL_ENFORCE_LICENSE=ON -DFOURREAL_LICENSE_API_URL=https://<ref>.supabase.co -DFOURREAL_LICENSE_ANON_KEY=<publishable key> -DFOURREAL_LICENSE_PUBLIC_KEY=<public key> ...`
   Without `ENFORCE=ON` (the default) every guitar is unlocked: use that for beta builds.

## Selling / giving guitars

- Serial numbers: `python3 tools/license/make_serials.py --guitars LP,STR --count 10 --note "Batch 1" --out serials_batch1`
  Give the `.csv` serials to buyers; run the `.sql` in the Supabase SQL editor (it stores only SHA-256 hashes).
  `--guitars ALL` makes a bundle. A serial works once.
- Give a guitar directly (gift, refund fix): `insert into entitlements (user_id, guitar_code, source) values ('<uuid>', 'LP', 'manual');`
- Take it away: `delete from entitlements where user_id = '<uuid>' and guitar_code = 'LP';` (the plugin loses it at its next refresh, or when the token expires)
- Free a computer slot: `delete from devices where id = '<uuid>';`

## Tests

- Server logic: `node --experimental-strip-types --test server/test/core.test.ts`
- Plugin verifier (Ed25519/SHA-512 against OpenSSL vectors, token rules): see `tests/license_test.cpp` (also a CMake target `license_test`).
- Cross-check: `LICENSE_CROSS_FILE=x.txt node --experimental-strip-types --test server/test/core.test.ts`, then `license_test tests/license/vectors.txt x.txt`.

## Limits to know

Offline checks cannot stop someone who patches the plugin binary; this protects against sharing files and accounts, not against
a determined cracker. A refreshed token is only issued online, so a revoked guitar disappears at the next refresh (max 30 days offline).
