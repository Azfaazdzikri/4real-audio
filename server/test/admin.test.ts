// Tests of the admin side (serials + prices) against an in-memory store.
// Run:  node --experimental-strip-types --test server/test/
import assert from "node:assert/strict";
import { test } from "node:test";

import { adminRoute, MAX_COUNT, newSerial } from "../supabase/functions/_shared/admin.ts";
import type { AdminStore, GuitarRow, SerialRow, SerialStatus } from "../supabase/functions/_shared/admin.ts";
import { normalizeSerial, sha256Hex } from "../supabase/functions/_shared/core.ts";

const SECRET = "a-long-random-admin-secret";

class MemoryAdminStore implements AdminStore {
  secretHash: string | null = null;
  guitars: GuitarRow[] = [
    ["LP", "Les Paul"], ["335", "ES-335"], ["SG", "SG"], ["PRS", "PRS SE Custom 24"],
    ["RIC", "Rickenbacker 330"], ["STR", "Stratocaster"], ["TCR", "Telecaster"], ["FLC", "Gretsch White Falcon"],
  ].map(([code, name]) => ({ code, name, price: 0, promo: 0 }));
  serials: (SerialRow & { hash: string })[] = [];
  n = 1;

  async getAdminSecretHash() {
    return this.secretHash;
  }
  async listGuitars() {
    return this.guitars.map((g) => ({ ...g }));
  }
  async setGuitarPrice(code: string, price: number, promo: number) {
    const g = this.guitars.find((x) => x.code === code);
    if (!g) return false;
    g.price = price;
    g.promo = promo;
    return true;
  }
  async insertSerials(rows: { serialHash: string; guitarCodes: string[]; note: string; recipientEmail: string }[]) {
    for (const r of rows) {
      assert.ok(!this.serials.some((s) => s.hash === r.serialHash), "duplicate hash");
      this.serials.push({
        hash: r.serialHash,
        id: `bbbbbbbb-0000-0000-0000-${String(this.n++).padStart(12, "0")}`,
        guitarCodes: r.guitarCodes,
        note: r.note,
        recipientEmail: r.recipientEmail,
        createdAt: new Date(1_700_000_000_000 + this.n * 1000).toISOString(),
        redeemedByEmail: "",
        redeemedAt: null,
      });
    }
  }
  async listSerials(q: { status: SerialStatus; limit: number; offset: number }) {
    const all = this.serials
      .filter((s) => (q.status === "unused" ? !s.redeemedAt : q.status === "used" ? !!s.redeemedAt : true))
      .sort((a, b) => b.createdAt.localeCompare(a.createdAt));
    return { rows: all.slice(q.offset, q.offset + q.limit), total: all.length };
  }
  async deleteUnusedSerial(id: string) {
    const i = this.serials.findIndex((s) => s.id === id);
    if (i < 0) return "missing" as const;
    if (this.serials[i].redeemedAt) return "used" as const;
    this.serials.splice(i, 1);
    return "deleted" as const;
  }
}

async function setup() {
  const store = new MemoryAdminStore();
  store.secretHash = await sha256Hex(SECRET);
  const call = (route: string, body: Record<string, unknown> = {}, secret = SECRET) => adminRoute(store, secret, route, body);
  return { store, call };
}

test("the secret is required: wrong, empty, and not set up are all refused", async () => {
  const { store, call } = await setup();
  assert.equal((await call("catalog", {}, "nope")).status, 403);
  assert.equal((await call("catalog", {}, "")).status, 403);
  assert.equal((await call("create-serials", { guitars: "ALL" }, "nope")).status, 403);
  assert.equal(store.serials.length, 0);
  store.secretHash = null;
  assert.equal((await call("catalog")).status, 503);
});

test("unknown route", async () => {
  const { call } = await setup();
  assert.equal((await call("nothing")).status, 404);
});

test("serials look like the ones tools/license/make_serials.py makes", () => {
  for (let i = 0; i < 200; i++) {
    assert.match(newSerial(), /^[ABCDEFGHJKLMNPQRSTUVWXYZ2-9]{5}(-[ABCDEFGHJKLMNPQRSTUVWXYZ2-9]{5}){3}$/);
  }
  assert.notEqual(newSerial(), newSerial());
});

test("create: one serial for one guitar, one for two guitars, one bundle with ALL", async () => {
  const { store, call } = await setup();
  const one = await call("create-serials", { guitars: ["lp"], count: 1, note: "promo" });
  assert.equal(one.status, 200);
  const two = await call("create-serials", { guitars: ["STR", "TCR"], count: 1 });
  const all = await call("create-serials", { guitars: "ALL", count: 1 });
  assert.deepEqual((one.body.serials as { guitarCodes: string[] }[])[0].guitarCodes, ["LP"]);
  assert.deepEqual((two.body.serials as { guitarCodes: string[] }[])[0].guitarCodes, ["STR", "TCR"]);
  assert.equal((all.body.serials as { guitarCodes: string[] }[])[0].guitarCodes.length, 8);
  assert.equal(store.serials.length, 3);
  assert.equal(store.serials[0].note, "promo");
});

test("create: only the SHA-256 is stored, and it is the hash redeem-key looks up", async () => {
  const { store, call } = await setup();
  const r = await call("create-serials", { guitars: ["SG"], count: 3 });
  const serials = r.body.serials as { serial: string }[];
  assert.equal(serials.length, 3);
  for (const s of serials) {
    const hash = await sha256Hex(normalizeSerial(s.serial));
    assert.ok(store.serials.some((row) => row.hash === hash));
    assert.ok(!JSON.stringify(store.serials).includes(s.serial), "plain serial must not be stored");
    // any spelling a buyer types gives the same hash
    assert.equal(await sha256Hex(normalizeSerial(s.serial.toLowerCase().replaceAll("-", " "))), hash);
  }
});

test("create: one serial per recipient email (cleaned, lower-cased, de-duplicated)", async () => {
  const { store, call } = await setup();
  const r = await call("create-serials", { guitars: ["LP", "SG"], recipients: [" Ann@Example.com ", "bob@example.com", "ann@example.com", ""], count: 99 });
  assert.equal(r.status, 200);
  const serials = r.body.serials as { recipientEmail: string }[];
  assert.deepEqual(serials.map((s) => s.recipientEmail), ["ann@example.com", "bob@example.com"]);
  assert.deepEqual(store.serials.map((s) => s.recipientEmail), ["ann@example.com", "bob@example.com"]);
});

test("create: bad input is refused and nothing is stored", async () => {
  const { store, call } = await setup();
  const bad: Record<string, unknown>[] = [
    {},
    { guitars: [] },
    { guitars: ["XX"] },
    { guitars: ["LP", ""] },
    { guitars: "LP" },
    { guitars: ["LP"], count: 0 },
    { guitars: ["LP"], count: MAX_COUNT + 1 },
    { guitars: ["LP"], count: 1.5 },
    { guitars: ["LP"], recipients: ["not-an-email"] },
    { guitars: ["LP"], recipients: "ann@example.com" },
    { guitars: ["LP"], recipients: Array.from({ length: 201 }, (_, i) => `u${i}@example.com`) },
  ];
  for (const b of bad) assert.equal((await call("create-serials", b)).status, 400, JSON.stringify(b).slice(0, 80));
  assert.equal(store.serials.length, 0);
});

test("create: count from a form (a string) works", async () => {
  const { store, call } = await setup();
  assert.equal((await call("create-serials", { guitars: ["LP"], count: "5" })).status, 200);
  assert.equal(store.serials.length, 5);
});

test("list: filters unused / used, newest first, paged; never shows the plain serial", async () => {
  const { store, call } = await setup();
  await call("create-serials", { guitars: ["LP"], count: 5 });
  store.serials[1].redeemedAt = "2026-11-18T00:00:00.000Z";
  store.serials[1].redeemedByEmail = "ann@example.com";
  const all = await call("list-serials", {});
  assert.equal(all.body.total, 5);
  const used = await call("list-serials", { status: "used" });
  assert.equal(used.body.total, 1);
  assert.equal((used.body.rows as SerialRow[])[0].redeemedByEmail, "ann@example.com");
  assert.equal((await call("list-serials", { status: "unused" })).body.total, 4);
  const page = await call("list-serials", { limit: 2, offset: 4 });
  assert.equal((page.body.rows as SerialRow[]).length, 1);
  assert.equal((await call("list-serials", { limit: 0 })).status, 400);
  assert.equal((await call("list-serials", { limit: 1000 })).status, 400);
  assert.ok(!JSON.stringify(all.body).toLowerCase().includes("hash"));
});

test("delete: an unused serial goes, a redeemed one stays, a missing one is 404", async () => {
  const { store, call } = await setup();
  await call("create-serials", { guitars: ["LP"], count: 2 });
  const [a, b] = store.serials;
  b.redeemedAt = "2026-11-18T00:00:00.000Z";
  assert.equal((await call("delete-serial", { id: a.id })).status, 200);
  assert.equal((await call("delete-serial", { id: b.id })).status, 409);
  assert.equal((await call("delete-serial", { id: a.id })).status, 404);
  assert.equal((await call("delete-serial", { id: "x" })).status, 400);
  assert.equal(store.serials.length, 1);
});

test("prices: set price and promo for several guitars; a bad row changes nothing", async () => {
  const { store, call } = await setup();
  const ok = await call("set-prices", { prices: [{ code: "lp", price: "149000", promo: 10 }, { code: "STR", price: 99000 }] });
  assert.equal(ok.status, 200);
  assert.deepEqual(store.guitars.filter((g) => g.price > 0).map((g) => [g.code, g.price, g.promo]), [["LP", 149000, 10], ["STR", 99000, 0]]);

  const before = JSON.stringify(store.guitars);
  for (const prices of [
    [{ code: "SG", price: 50000 }, { code: "XX", price: 1 }],
    [{ code: "SG", price: 50000 }, { code: "TCR", price: -1 }],
    [{ code: "SG", price: 50000 }, { code: "TCR", price: 1.5 }],
    [{ code: "SG", price: 50000, promo: 101 }],
    [{ code: "SG", price: "abc" }],
    [],
  ]) {
    assert.equal((await call("set-prices", { prices })).status, 400, JSON.stringify(prices));
  }
  assert.equal(JSON.stringify(store.guitars), before);
});

test("catalog lists the 8 guitars with price and promo", async () => {
  const { call } = await setup();
  const r = await call("catalog");
  assert.equal((r.body.guitars as GuitarRow[]).length, 8);
  assert.deepEqual(Object.keys((r.body.guitars as GuitarRow[])[0]).sort(), ["code", "name", "price", "promo"]);
});
