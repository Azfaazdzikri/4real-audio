// Licence server core: token signing, serial handling and the four request handlers (device-login, device-refresh,
// device-logout, redeem-key). It only uses web-standard APIs (crypto.subtle, TextEncoder, btoa) and talks to the database
// through the small `Store` interface, so the same code runs in Supabase Edge Functions (Deno), in Node (tests) and against an
// in-memory store (tests). The token format is documented in Source/license/Token.h.

export interface Config {
  /** Ed25519 private key, PKCS#8 DER, base64. Made by tools/license/gen_keys.py, stored as the secret LICENSE_SIGNING_KEY. */
  signingKeyPkcs8: string;
  /** Computers one account may be signed in on at the same time. */
  maxDevices: number;
  /** Lifetime of a token. The plugin renews it whenever it is online, so this is the offline grace period. */
  tokenDays: number;
  /** Unix time source, replaceable in tests. */
  now?: () => number;
  /** Site where the player approves a browser sign-in, no trailing slash (default https://3ffect.my.id). */
  accountUrl?: string;
}

/** A sign-in started by the plugin that the player approves in the browser (device-code flow). */
export interface LoginRequestRow {
  id: string;
  secretHash: string;
  machineId: string;
  machineName: string;
  status: "pending" | "approved" | "consumed";
  userId: string | null;
  /** Unix seconds. */
  expiresAt: number;
}

export interface DeviceRow {
  id: string;
  userId: string;
  machineId: string;
  machineName: string;
  secretHash: string;
}

export interface Store {
  getUserFromJwt(jwt: string): Promise<{ id: string; email: string } | null>;
  /** Checks an email + password against the account site (3ffect.my.id) and returns the matching licence account. */
  signInWithPassword(
    email: string,
    password: string,
  ): Promise<{ ok: true; user: { id: string; email: string } } | { ok: false; reason: "invalid" | "unconfirmed" | "throttled" | "unavailable" }>;
  getUserEmail(userId: string): Promise<string>;
  getEntitlements(userId: string): Promise<string[]>;
  findDevice(userId: string, machineId: string): Promise<DeviceRow | null>;
  getDeviceById(id: string): Promise<DeviceRow | null>;
  countDevices(userId: string): Promise<number>;
  /** Creates the device, or, if this account already has this machine, replaces its secret. */
  upsertDevice(row: { userId: string; machineId: string; machineName: string; secretHash: string }): Promise<DeviceRow>;
  touchDevice(id: string): Promise<void>;
  deleteDevice(id: string): Promise<void>;
  /** Atomic: marks the serial as used and adds the guitars to the account. */
  redeemSerial(userId: string, serialHash: string): Promise<{ ok: true; codes: string[] } | { ok: false; reason: "invalid" | "used" }>;

  // --- browser sign-in -----------------------------------------------------------------------------------------------
  /** The licence account of an email address (made on first use). The email has already been verified by the account site. */
  userForEmail(email: string): Promise<{ id: string; email: string }>;
  /** Hash (sha-256 hex) of the secret that the account site presents when it approves a request, or null when it is not set up. */
  getApproveSecretHash(): Promise<string | null>;
  /** Stores a pending request; false when the code is already taken. */
  createLoginRequest(r: { code: string; secretHash: string; machineId: string; machineName: string; expiresAt: number }): Promise<{ id: string } | null>;
  getLoginRequest(id: string): Promise<LoginRequestRow | null>;
  /** pending -> approved for a code that has not expired. Returns the computer name, or null when there is no such request. */
  approveLoginRequest(code: string, userId: string, now: number): Promise<{ machineName: string } | null>;
  /** approved -> consumed, once. false when somebody else got there first. */
  consumeLoginRequest(id: string, now: number): Promise<boolean>;
  /** Deletes requests that have expired. */
  purgeLoginRequests(now: number): Promise<void>;
}

export interface Result {
  status: number;
  body: Record<string, unknown>;
}

// ---------------------------------------------------------------------------------------------------------------------
const enc = new TextEncoder();

export function b64uEncode(bytes: Uint8Array): string {
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

export function b64Decode(text: string): Uint8Array {
  const s = text.replace(/-/g, "+").replace(/_/g, "/");
  const bin = atob(s + "=".repeat((4 - (s.length % 4)) % 4));
  return Uint8Array.from(bin, (c) => c.charCodeAt(0));
}

export async function sha256Hex(text: string): Promise<string> {
  const digest = await crypto.subtle.digest("SHA-256", enc.encode(text));
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

/** Serials are compared without case, spaces and dashes: "abcde-fghij ..." == "ABCDEFGHIJ...". Must match tools/license/make_serials.py. */
export function normalizeSerial(text: string): string {
  return text.toUpperCase().replace(/[^A-Z0-9]/g, "");
}

export function randomSecret(): string {
  return b64uEncode(crypto.getRandomValues(new Uint8Array(32)));
}

export function timingSafeEqual(a: string, b: string): boolean {
  const x = enc.encode(a), y = enc.encode(b);
  let diff = x.length ^ y.length;
  for (let i = 0; i < Math.max(x.length, y.length); i++) diff |= (x[i] ?? 0) ^ (y[i] ?? 0);
  return diff === 0;
}

// ---------------------------------------------------------------------------------------------------------------------
export interface TokenInput {
  userId: string;
  email: string;
  deviceId: string;
  machineId: string;
  guitars: string[];
}

/** token = base64url(payload JSON) + "." + base64url(Ed25519 signature of the payload bytes) */
export async function signToken(cfg: Config, t: TokenInput): Promise<string> {
  const now = (cfg.now ?? (() => Math.floor(Date.now() / 1000)))();
  const payload = JSON.stringify({
    v: 1,
    sub: t.userId,
    email: t.email,
    dev: t.deviceId,
    mid: t.machineId,
    guitars: t.guitars,
    iat: now,
    exp: now + Math.round(cfg.tokenDays * 86400),
  });
  const bytes = enc.encode(payload);
  const key = await crypto.subtle.importKey("pkcs8", b64Decode(cfg.signingKeyPkcs8), "Ed25519", false, ["sign"]);
  const signature = new Uint8Array(await crypto.subtle.sign("Ed25519", key, bytes));
  return b64uEncode(bytes) + "." + b64uEncode(signature);
}

// ---------------------------------------------------------------------------------------------------------------------
function fail(status: number, error: string, message: string): Result {
  return { status, body: { error, message } };
}

function isText(v: unknown, min: number, max: number): v is string {
  return typeof v === "string" && v.length >= min && v.length <= max;
}

async function tokenFor(store: Store, cfg: Config, device: DeviceRow, email: string): Promise<{ token: string; guitars: string[] }> {
  const guitars = (await store.getEntitlements(device.userId)).slice().sort();
  const token = await signToken(cfg, { userId: device.userId, email, deviceId: device.id, machineId: device.machineId, guitars });
  return { token, guitars };
}

/** The computer is identified by its device id + secret (no password is ever stored in the plugin). */
async function authDevice(store: Store, body: Record<string, unknown>): Promise<DeviceRow | Result> {
  const { deviceId, deviceSecret, machineId } = body;
  if (!isText(deviceId, 8, 64) || !isText(deviceSecret, 16, 200) || !isText(machineId, 8, 128)) {
    return fail(400, "bad_request", "Missing or invalid fields.");
  }
  const device = await store.getDeviceById(deviceId);
  // The same answer for "no such device" and "wrong secret", so ids cannot be probed.
  if (!device || !timingSafeEqual(device.secretHash, await sha256Hex(deviceSecret))) {
    return fail(403, "device_revoked", "This computer is no longer signed in to your account. Please sign in again.");
  }
  if (device.machineId !== machineId) {
    return fail(409, "machine_changed", "This computer does not match the one that was registered. Sign out and sign in again.");
  }
  return device;
}

const isResult = (v: DeviceRow | Result): v is Result => "status" in v;

// ---------------------------------------------------------------------------------------------------------------------
/** POST device-login: email + password (what the plugin sends) or Authorization: Bearer <access token>. Registers this computer. */
export async function deviceLogin(store: Store, cfg: Config, bearer: string, body: Record<string, unknown>): Promise<Result> {
  const { machineId, machineName, email, password } = body;
  if (!isText(machineId, 8, 128)) return fail(400, "bad_request", "Missing or invalid machine id.");
  const name = typeof machineName === "string" ? machineName.slice(0, 120) : "";

  let user: { id: string; email: string } | null = null;
  if (typeof email === "string" || typeof password === "string") {
    if (!isText(email, 3, 254) || !isText(password, 1, 200)) return fail(400, "bad_request", "Enter your email and password.");
    const r = await store.signInWithPassword(email.trim(), password);
    if (!r.ok) {
      if (r.reason === "unconfirmed") return fail(403, "email_not_confirmed", "Confirm your email first (check your inbox), then sign in.");
      if (r.reason === "throttled") return fail(429, "too_many_attempts", "Too many failed sign-ins. Wait a few minutes and try again.");
      if (r.reason === "unavailable") return fail(503, "account_server_unavailable", "The account server (3ffect.my.id) could not be reached. Try again in a minute.");
      return fail(401, "invalid_login", "Wrong email or password.");
    }
    user = r.user;
  } else {
    user = bearer ? await store.getUserFromJwt(bearer) : null;
    if (!user) return fail(401, "invalid_session", "Your sign-in is not valid. Please try again.");
  }

  const existing = await store.findDevice(user.id, machineId);
  if (!existing) {
    const count = await store.countDevices(user.id);
    if (count >= cfg.maxDevices) {
      return fail(409, "device_limit", `This account is already signed in on ${cfg.maxDevices} computers. Sign out on one of them first.`);
    }
  }

  const secret = randomSecret();
  const device = await store.upsertDevice({ userId: user.id, machineId, machineName: name, secretHash: await sha256Hex(secret) });
  const { token, guitars } = await tokenFor(store, cfg, device, user.email);
  return { status: 200, body: { deviceId: device.id, deviceSecret: secret, token, guitars } };
}

/** POST device-refresh: a fresh token with the account's current guitars. */
export async function deviceRefresh(store: Store, cfg: Config, body: Record<string, unknown>): Promise<Result> {
  const device = await authDevice(store, body);
  if (isResult(device)) return device;
  await store.touchDevice(device.id);
  const { token, guitars } = await tokenFor(store, cfg, device, await store.getUserEmail(device.userId));
  return { status: 200, body: { token, guitars } };
}

/** POST device-logout: frees the computer slot. Always answers 200 so a repeated call is harmless. */
export async function deviceLogout(store: Store, body: Record<string, unknown>): Promise<Result> {
  const { deviceId, deviceSecret } = body;
  if (isText(deviceId, 8, 64) && isText(deviceSecret, 16, 200)) {
    const device = await store.getDeviceById(deviceId);
    if (device && timingSafeEqual(device.secretHash, await sha256Hex(deviceSecret))) await store.deleteDevice(device.id);
  }
  return { status: 200, body: { ok: true } };
}

/** POST redeem-key: activates the guitars of a serial number for the account of this computer. */
export async function redeemKey(store: Store, cfg: Config, body: Record<string, unknown>): Promise<Result> {
  const device = await authDevice(store, body);
  if (isResult(device)) return device;
  const serial = typeof body.serial === "string" ? normalizeSerial(body.serial) : "";
  if (serial.length < 12 || serial.length > 64) return fail(404, "invalid_serial", "This serial number is not valid.");

  const redeemed = await store.redeemSerial(device.userId, await sha256Hex(serial));
  if (!redeemed.ok) {
    return redeemed.reason === "used"
      ? fail(409, "serial_used", "This serial number has already been used.")
      : fail(404, "invalid_serial", "This serial number is not valid.");
  }
  const { token, guitars } = await tokenFor(store, cfg, device, await store.getUserEmail(device.userId));
  return { status: 200, body: { token, guitars, added: redeemed.codes } };
}

// ---------------------------------------------------------------------------------------------------------------------
// Browser sign-in ("device code" flow). The plugin starts a request and shows its code, the player opens the account site,
// signs in there (any way, Google included) and approves the code, and the plugin, which keeps asking, receives its device.
//   plugin  -> browser-login/start -> { requestId, pollSecret, code, authorizeUrl }
//   browser -> account site          -> browser-login/approve (server to server, with the shared secret)
//   plugin  -> browser-login/poll  -> { status: "pending" } ... { status: "approved", deviceId, deviceSecret, token, guitars }

const REQUEST_SECONDS = 600;
const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // 32 letters and digits, no 0/O/1/I; 256 % 32 == 0, so no modulo bias

const nowOf = (cfg: Config) => (cfg.now ?? (() => Math.floor(Date.now() / 1000)))();

function makeCode(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(8));
  const chars = [...bytes].map((b) => CODE_ALPHABET[b % 32]).join("");
  return chars.slice(0, 4) + "-" + chars.slice(4);
}

/** "abcd efgh" -> "ABCD-EFGH"; null when it cannot be a code. */
export function normalizeCode(text: string): string | null {
  const s = text.toUpperCase().replace(/[^A-Z0-9]/g, "");
  return s.length === 8 ? s.slice(0, 4) + "-" + s.slice(4) : null;
}

/** POST browser-login/start: { machineId, machineName } */
export async function browserLoginStart(store: Store, cfg: Config, body: Record<string, unknown>): Promise<Result> {
  const { machineId, machineName } = body;
  if (!isText(machineId, 8, 128)) return fail(400, "bad_request", "Missing or invalid machine id.");
  const name = typeof machineName === "string" ? machineName.slice(0, 120) : "";
  const now = nowOf(cfg);
  await store.purgeLoginRequests(now);

  const pollSecret = randomSecret();
  for (let attempt = 0; attempt < 5; attempt++) {
    const code = makeCode();
    const created = await store.createLoginRequest({ code, secretHash: await sha256Hex(pollSecret), machineId, machineName: name, expiresAt: now + REQUEST_SECONDS });
    if (created) {
      const site = (cfg.accountUrl ?? "https://3ffect.my.id").replace(/\/+$/, "");
      return {
        status: 200,
        body: { requestId: created.id, pollSecret, code, expiresIn: REQUEST_SECONDS, authorizeUrl: `${site}/plugin/authorize?code=${code}` },
      };
    }
  }
  throw new Error("could not find a free login code");
}

/** POST browser-login/poll: { requestId, pollSecret } */
export async function browserLoginPoll(store: Store, cfg: Config, body: Record<string, unknown>): Promise<Result> {
  const { requestId, pollSecret } = body;
  if (!isText(requestId, 8, 64) || !isText(pollSecret, 16, 200)) return fail(400, "bad_request", "Missing or invalid fields.");
  const row = await store.getLoginRequest(requestId);
  // the same answer for "no such request" and "wrong secret"
  if (!row || !timingSafeEqual(row.secretHash, await sha256Hex(pollSecret))) {
    return fail(403, "login_request_invalid", "This sign-in request is not valid. Start again.");
  }
  const now = nowOf(cfg);
  if (row.status === "consumed" || row.expiresAt <= now) return fail(410, "login_expired", "This sign-in request has expired. Start again.");
  if (row.status === "pending" || !row.userId) return { status: 200, body: { status: "pending" } };

  const userId = row.userId;
  const existing = await store.findDevice(userId, row.machineId);
  if (!existing && (await store.countDevices(userId)) >= cfg.maxDevices) {
    return fail(409, "device_limit", `This account is already signed in on ${cfg.maxDevices} computers. Sign out on one of them first.`);
  }
  if (!(await store.consumeLoginRequest(row.id, now))) return fail(410, "login_expired", "This sign-in request has already been used. Start again.");

  const secret = randomSecret();
  const device = await store.upsertDevice({ userId, machineId: row.machineId, machineName: row.machineName, secretHash: await sha256Hex(secret) });
  const { token, guitars } = await tokenFor(store, cfg, device, await store.getUserEmail(userId));
  return { status: 200, body: { status: "approved", deviceId: device.id, deviceSecret: secret, token, guitars } };
}

/**
 * POST browser-login/approve: { code, email }, header x-approve-secret. Called by the account site after the player signed in
 * there and pressed "allow". The shared secret proves the call comes from the account site, so `email` can be trusted.
 */
export async function browserLoginApprove(store: Store, cfg: Config, secret: string, body: Record<string, unknown>): Promise<Result> {
  const expected = await store.getApproveSecretHash();
  if (!expected) return fail(503, "not_configured", "Browser sign-in is not set up on this server.");
  if (!secret || !timingSafeEqual(expected, await sha256Hex(secret))) return fail(403, "forbidden", "Not allowed.");

  const { code, email } = body;
  if (typeof code !== "string" || !isText(email, 3, 254)) return fail(400, "bad_request", "Missing or invalid fields.");
  const clean = normalizeCode(code);
  if (!clean) return fail(404, "code_invalid", "This code is not valid or has expired.");

  const user = await store.userForEmail(email.trim().toLowerCase());
  const approved = await store.approveLoginRequest(clean, user.id, nowOf(cfg));
  if (!approved) return fail(404, "code_invalid", "This code is not valid or has expired.");
  return { status: 200, body: { ok: true, machineName: approved.machineName } };
}
