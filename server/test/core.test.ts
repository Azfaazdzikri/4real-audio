// Tests of the licence server logic against an in-memory store.
// Run:  node --experimental-strip-types --test server/test/core.test.ts
// Optional: set LICENSE_CROSS_FILE=<path> to also write tokens made by this server code in the format of tests/license/token_cases.txt,
// so the C++ plugin verifier can be run on them (see server/README.md, "Tests").
import assert from "node:assert/strict";
import { writeFileSync } from "node:fs";
import { test } from "node:test";

import {
  b64Decode,
  b64uEncode,
  deviceLogin,
  deviceLogout,
  deviceRefresh,
  normalizeSerial,
  redeemKey,
  sha256Hex,
} from "../supabase/functions/_shared/core.ts";
import type { Config, DeviceRow, Store } from "../supabase/functions/_shared/core.ts";

// --- in-memory store ---------------------------------------------------------------------------------------------
class MemoryStore implements Store {
  users = new Map<string, { id: string; email: string }>(); // jwt -> user
  entitlements = new Map<string, Set<string>>();
  devices = new Map<string, DeviceRow>();
  serials = new Map<string, { codes: string[]; usedBy?: string }>();
  nextId = 1;

  addUser(jwt: string, id: string, email: string) {
    this.users.set(jwt, { id, email });
    this.entitlements.set(id, new Set());
  }
  async addSerial(serial: string, codes: string[]) {
    this.serials.set(await sha256Hex(normalizeSerial(serial)), { codes });
  }
  async getUserFromJwt(jwt: string) {
    return this.users.get(jwt) ?? null;
  }
  async getUserEmail(userId: string) {
    return [...this.users.values()].find((u) => u.id === userId)?.email ?? "";
  }
  async getEntitlements(userId: string) {
    return [...(this.entitlements.get(userId) ?? [])];
  }
  async findDevice(userId: string, machineId: string) {
    return [...this.devices.values()].find((d) => d.userId === userId && d.machineId === machineId) ?? null;
  }
  async getDeviceById(id: string) {
    return this.devices.get(id) ?? null;
  }
  async countDevices(userId: string) {
    return [...this.devices.values()].filter((d) => d.userId === userId).length;
  }
  async upsertDevice(r: { userId: string; machineId: string; machineName: string; secretHash: string }) {
    const old = await this.findDevice(r.userId, r.machineId);
    const row: DeviceRow = { id: old?.id ?? `00000000-0000-0000-0000-${String(this.nextId++).padStart(12, "0")}`, ...r };
    this.devices.set(row.id, row);
    return row;
  }
  async touchDevice(_id: string) {}
  async deleteDevice(id: string) {
    this.devices.delete(id);
  }
  async redeemSerial(userId: string, hash: string) {
    const s = this.serials.get(hash);
    if (!s) return { ok: false as const, reason: "invalid" as const };
    if (s.usedBy) return { ok: false as const, reason: "used" as const };
    s.usedBy = userId;
    for (const c of s.codes) this.entitlements.get(userId)!.add(c);
    return { ok: true as const, codes: s.codes };
  }
}

// --- helpers -------------------------------------------------------------------------------------------------------
const NOW = 1_800_000_000;

async function makeKey() {
  const pair = (await crypto.subtle.generateKey("Ed25519", true, ["sign", "verify"])) as CryptoKeyPair;
  const pkcs8 = new Uint8Array(await crypto.subtle.exportKey("pkcs8", pair.privateKey));
  return { pair, pkcs8: btoa(String.fromCharCode(...pkcs8)) };
}

async function setup(overrides: Partial<Config> = {}) {
  const key = await makeKey();
  const cfg: Config = { signingKeyPkcs8: key.pkcs8, maxDevices: 3, tokenDays: 30, now: () => NOW, ...overrides };
  const store = new MemoryStore();
  store.addUser("jwt-alice", "11111111-1111-1111-1111-111111111111", "alice@example.com");
  store.addUser("jwt-bob", "22222222-2222-2222-2222-222222222222", "bob@example.com");
  return { key, cfg, store };
}

function decodeToken(token: string) {
  const [p, s] = token.split(".");
  return { payloadBytes: b64Decode(p), signature: b64Decode(s), payload: JSON.parse(new TextDecoder().decode(b64Decode(p))) };
}

const MACHINE_A = "machine-aaaaaaaaaaaaaaaa";
const MACHINE_B = "machine-bbbbbbbbbbbbbbbb";

async function login(ctx: Awaited<ReturnType<typeof setup>>, machineId = MACHINE_A, jwt = "jwt-alice") {
  const r = await deviceLogin(ctx.store, ctx.cfg, jwt, { machineId, machineName: "Test PC" });
  assert.equal(r.status, 200, JSON.stringify(r.body));
  return r.body as { deviceId: string; deviceSecret: string; token: string; guitars: string[] };
}

// --- tests ---------------------------------------------------------------------------------------------------------
test("serial hash matches the one tools/license/make_serials.py writes into the SQL", async () => {
  assert.equal(normalizeSerial("sr3u3-kg6y3 86bk4-k2vex"), "SR3U3KG6Y386BK4K2VEX");
  assert.equal(await sha256Hex(normalizeSerial("SR3U3-KG6Y3-86BK4-K2VEX")), "a6762aaa750cb3a821fbccffe0d87ac6b52c96fb8df0da8b206f8f607833fbc3");
});

test("login makes a signed token bound to the computer, with the account's guitars", async () => {
  const ctx = await setup();
  ctx.store.entitlements.get("11111111-1111-1111-1111-111111111111")!.add("STR").add("LP");
  const r = await login(ctx);
  assert.deepEqual(r.guitars, ["LP", "STR"]);
  assert.ok(r.deviceSecret.length >= 40);

  const { payloadBytes, signature, payload } = decodeToken(r.token);
  assert.deepEqual(payload, {
    v: 1, sub: "11111111-1111-1111-1111-111111111111", email: "alice@example.com", dev: r.deviceId, mid: MACHINE_A,
    guitars: ["LP", "STR"], iat: NOW, exp: NOW + 30 * 86400,
  });
  assert.ok(await crypto.subtle.verify("Ed25519", ctx.key.pair.publicKey, signature, payloadBytes), "signature verifies with the public key");
  const tampered = payloadBytes.slice();
  tampered[10] ^= 1;
  assert.ok(!(await crypto.subtle.verify("Ed25519", ctx.key.pair.publicKey, signature, tampered)), "a changed payload does not verify");
});

test("a new account has a valid token with no guitars (everything locked)", async () => {
  const ctx = await setup();
  const r = await login(ctx, MACHINE_A, "jwt-bob");
  assert.deepEqual(r.guitars, []);
  assert.deepEqual(decodeToken(r.token).payload.guitars, []);
});

test("login needs a valid session and a machine id", async () => {
  const ctx = await setup();
  assert.equal((await deviceLogin(ctx.store, ctx.cfg, "", { machineId: MACHINE_A })).status, 401);
  assert.equal((await deviceLogin(ctx.store, ctx.cfg, "jwt-nobody", { machineId: MACHINE_A })).status, 401);
  assert.equal((await deviceLogin(ctx.store, ctx.cfg, "jwt-alice", {})).status, 400);
  assert.equal((await deviceLogin(ctx.store, ctx.cfg, "jwt-alice", { machineId: "short" })).status, 400);
  assert.equal((await deviceLogin(ctx.store, ctx.cfg, "jwt-alice", { machineId: 12345678 })).status, 400);
});

test("computer limit: the 4th computer is refused, signing in again on a known computer is not", async () => {
  const ctx = await setup();
  for (const m of ["machine-1111111111", "machine-2222222222", "machine-3333333333"]) await login(ctx, m);
  const fourth = await deviceLogin(ctx.store, ctx.cfg, "jwt-alice", { machineId: "machine-4444444444" });
  assert.equal(fourth.status, 409);
  assert.equal(fourth.body.error, "device_limit");

  const again = await login(ctx, "machine-2222222222");
  assert.equal(ctx.store.devices.size, 3);
  assert.ok(again.deviceSecret);

  // other accounts have their own slots
  await login(ctx, "machine-4444444444", "jwt-bob");
});

test("signing in again on the same computer replaces the old device secret", async () => {
  const ctx = await setup();
  const first = await login(ctx);
  const second = await login(ctx);
  assert.equal(first.deviceId, second.deviceId);
  const oldSecret = await deviceRefresh(ctx.store, ctx.cfg, { deviceId: first.deviceId, deviceSecret: first.deviceSecret, machineId: MACHINE_A });
  assert.equal(oldSecret.status, 403);
  const newSecret = await deviceRefresh(ctx.store, ctx.cfg, { deviceId: second.deviceId, deviceSecret: second.deviceSecret, machineId: MACHINE_A });
  assert.equal(newSecret.status, 200);
});

test("refresh: wrong secret, unknown device and other computer are refused", async () => {
  const ctx = await setup();
  const d = await login(ctx);
  const wrong = await deviceRefresh(ctx.store, ctx.cfg, { deviceId: d.deviceId, deviceSecret: "x".repeat(43), machineId: MACHINE_A });
  assert.equal(wrong.status, 403);
  assert.equal(wrong.body.error, "device_revoked");
  const unknown = await deviceRefresh(ctx.store, ctx.cfg, { deviceId: "99999999-9999-9999-9999-999999999999", deviceSecret: d.deviceSecret, machineId: MACHINE_A });
  assert.equal(unknown.status, 403);
  assert.deepEqual(unknown.body, wrong.body); // no way to tell the two cases apart
  const moved = await deviceRefresh(ctx.store, ctx.cfg, { deviceId: d.deviceId, deviceSecret: d.deviceSecret, machineId: MACHINE_B });
  assert.equal(moved.status, 409);
  assert.equal(moved.body.error, "machine_changed");
  assert.equal((await deviceRefresh(ctx.store, ctx.cfg, { deviceId: d.deviceId })).status, 400);
});

test("serial: redeem unlocks the guitars, works with any spelling, and only once", async () => {
  const ctx = await setup();
  await ctx.store.addSerial("K7M4Q-2XB9D-RT6WH-P3NVC", ["LP", "STR"]);
  const d = await login(ctx);
  assert.deepEqual(d.guitars, []);

  const r = await redeemKey(ctx.store, ctx.cfg, { deviceId: d.deviceId, deviceSecret: d.deviceSecret, machineId: MACHINE_A, serial: " k7m4q 2xb9d-rt6wh p3nvc " });
  assert.equal(r.status, 200, JSON.stringify(r.body));
  assert.deepEqual(r.body.added, ["LP", "STR"]);
  assert.deepEqual(decodeToken(r.body.token as string).payload.guitars, ["LP", "STR"]);

  // a refresh now includes them, a second use of the serial is refused, even for another account
  const refreshed = await deviceRefresh(ctx.store, ctx.cfg, { deviceId: d.deviceId, deviceSecret: d.deviceSecret, machineId: MACHINE_A });
  assert.deepEqual(refreshed.body.guitars, ["LP", "STR"]);
  const again = await redeemKey(ctx.store, ctx.cfg, { deviceId: d.deviceId, deviceSecret: d.deviceSecret, machineId: MACHINE_A, serial: "K7M4Q-2XB9D-RT6WH-P3NVC" });
  assert.equal(again.status, 409);
  assert.equal(again.body.error, "serial_used");
  const b = await login(ctx, MACHINE_B, "jwt-bob");
  const stolen = await redeemKey(ctx.store, ctx.cfg, { deviceId: b.deviceId, deviceSecret: b.deviceSecret, machineId: MACHINE_B, serial: "K7M4Q-2XB9D-RT6WH-P3NVC" });
  assert.equal(stolen.status, 409);
  assert.deepEqual((await deviceRefresh(ctx.store, ctx.cfg, { deviceId: b.deviceId, deviceSecret: b.deviceSecret, machineId: MACHINE_B })).body.guitars, []);
});

test("serial: wrong, empty and too short serials are refused; the device must be valid", async () => {
  const ctx = await setup();
  await ctx.store.addSerial("K7M4Q-2XB9D-RT6WH-P3NVC", ["FLC"]);
  const d = await login(ctx);
  const base = { deviceId: d.deviceId, deviceSecret: d.deviceSecret, machineId: MACHINE_A };
  for (const serial of ["AAAAA-AAAAA-AAAAA-AAAAA", "", "ABC", 12345, undefined]) {
    const r = await redeemKey(ctx.store, ctx.cfg, { ...base, serial });
    assert.equal(r.status, 404, `serial ${String(serial)}`);
    assert.equal(r.body.error, "invalid_serial");
  }
  const noAuth = await redeemKey(ctx.store, ctx.cfg, { ...base, deviceSecret: "y".repeat(43), serial: "K7M4Q-2XB9D-RT6WH-P3NVC" });
  assert.equal(noAuth.status, 403);
  assert.deepEqual([...(ctx.store.serials.values())][0].usedBy, undefined, "a refused request must not use up the serial");
});

test("logout frees the slot and the old secret stops working; a wrong secret deletes nothing", async () => {
  const ctx = await setup();
  const d = await login(ctx);
  await deviceLogout(ctx.store, { deviceId: d.deviceId, deviceSecret: "z".repeat(43) });
  assert.equal(ctx.store.devices.size, 1);
  assert.equal((await deviceLogout(ctx.store, { deviceId: d.deviceId, deviceSecret: d.deviceSecret })).status, 200);
  assert.equal(ctx.store.devices.size, 0);
  assert.equal((await deviceLogout(ctx.store, { deviceId: d.deviceId, deviceSecret: d.deviceSecret })).status, 200, "idempotent");
  assert.equal((await deviceRefresh(ctx.store, ctx.cfg, { deviceId: d.deviceId, deviceSecret: d.deviceSecret, machineId: MACHINE_A })).body.error, "device_revoked");
});

test("base64url helpers round trip", () => {
  const bytes = Uint8Array.from([0xfb, 0xff, 0xfe, 0, 1]);
  assert.equal(b64uEncode(bytes), "-__-AAE");
  assert.deepEqual(b64Decode("-__-AAE"), bytes);
});

test("writes cross-language cases for the C++ verifier (only when LICENSE_CROSS_FILE is set)", async () => {
  const file = process.env.LICENSE_CROSS_FILE;
  if (!file) return;
  const ctx = await setup();
  ctx.store.entitlements.get("11111111-1111-1111-1111-111111111111")!.add("LP").add("335").add("FLC");
  const d = await login(ctx);
  const rawPub = new Uint8Array(await crypto.subtle.exportKey("raw", ctx.key.pair.publicKey));
  const other = await setup();
  const otherToken = (await login(other)).token;
  const lines = [
    `KEY ${b64uEncode(rawPub)}`,
    `MID ${MACHINE_A}`,
    `NOW ${NOW + 1000}`,
    `CASE server_made_ok Ok 335,FLC,LP ${d.token}`,
    `CASE server_token_other_key BadSignature - ${otherToken}`,
    `CASE server_token_expired Expired - ${d.token}`.replace("NOW", "NOW"),
  ];
  // the expired case needs a later clock: a second block with its own NOW is not possible in one file, so make a short-lived token instead
  const shortLived = await setup({ tokenDays: 0.0001, signingKeyPkcs8: ctx.key.pkcs8 });
  shortLived.cfg.now = () => NOW - 1000;
  const sl = await login(shortLived);
  lines[5] = `CASE server_token_expired Expired - ${sl.token}`;
  writeFileSync(file, lines.join("\n") + "\n");
});
