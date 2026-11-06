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
/** POST device-login: Authorization: Bearer <access token of the account>. Registers this computer. */
export async function deviceLogin(store: Store, cfg: Config, bearer: string, body: Record<string, unknown>): Promise<Result> {
  const { machineId, machineName } = body;
  if (!isText(machineId, 8, 128)) return fail(400, "bad_request", "Missing or invalid machine id.");
  const name = typeof machineName === "string" ? machineName.slice(0, 120) : "";

  const user = bearer ? await store.getUserFromJwt(bearer) : null;
  if (!user) return fail(401, "invalid_session", "Your sign-in is not valid. Please try again.");

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
