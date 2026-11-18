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
6. Build the plugin. There is no unlocked build: every build enforces the licences. The server URL, the publishable key and the
   PUBLIC key are set in `CMakeLists.txt` (change them there, or pass `-DFOURREAL_LICENSE_API_URL=...`, `-DFOURREAL_LICENSE_ANON_KEY=...`,
   `-DFOURREAL_LICENSE_PUBLIC_KEY=...` for another server). The installers are built by GitHub Actions (`.github/workflows/build.yml`).

## Selling / giving guitars

- Serial numbers: `python3 tools/license/make_serials.py --guitars LP,STR --count 10 --note "Batch 1" --out serials_batch1`
  Give the `.csv` serials to buyers; run the `.sql` in the Supabase SQL editor (it stores only SHA-256 hashes).
  `--guitars ALL` makes a bundle. A serial works once.
- Give a guitar directly (gift, refund fix): `insert into entitlements (user_id, guitar_code, source) values ('<uuid>', 'LP', 'manual');`
- Take it away: `delete from entitlements where user_id = '<uuid>' and guitar_code = 'LP';` (the plugin loses it at its next refresh, or when the token expires)
- Free a computer slot: `delete from devices where id = '<uuid>';`

## Admin from the 3ffect tab (serials, prices)

The **4real Audio** tab in the 3ffect admin makes serial numbers (one per recipient email, which 3ffect then emails), lists and deletes unused
serials, and edits the price per guitar. It talks to the `admin-serials` function; the 3ffect *server* (never the browser) calls it with a shared secret.

1. `supabase db push` (adds `guitars.price`, `guitars.promo`, `serials.recipient_email`) and `supabase functions deploy admin-serials`.
2. Pick a long random secret and store its SHA-256 here (SQL editor): `insert into public.licence_settings (key, value) values ('admin_secret_sha256', encode(sha256('<secret>'::bytea), 'hex')) on conflict (key) do update set value = excluded.value;`
3. Give the same secret to the 3ffect serverapp as `FOURREAL_ADMIN_SECRET` (and `FOURREAL_ADMIN_URL` if the project differs). Change both to rotate it.

Routes (POST, header `x-admin-secret`): `catalog`, `set-prices`, `create-serials`, `list-serials`, `delete-serial`; see `_shared/admin.ts`.
Plain serials are returned once by `create-serials` and never stored. A redeemed serial cannot be deleted; take the guitar away with the SQL above instead.

## Tests

- Server logic: `node --experimental-strip-types --test server/test/core.test.ts server/test/admin.test.ts`
- Plugin verifier (Ed25519/SHA-512 against OpenSSL vectors, token rules): see `tests/license_test.cpp` (also a CMake target `license_test`).
- Cross-check: `LICENSE_CROSS_FILE=x.txt node --experimental-strip-types --test server/test/core.test.ts`, then `license_test tests/license/vectors.txt x.txt`.

## Limits to know

Offline checks cannot stop someone who patches the plugin binary; this protects against sharing files and accounts, not against
a determined cracker. A refreshed token is only issued online, so a revoked guitar disappears at the next refresh (max 30 days offline).
