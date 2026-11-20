// Admin side of the licence server, called only by the 3ffect.my.id server (never by the plugin or by players):
// make serial numbers (optionally one per recipient email), list / delete them, and manage the guitar prices.
// Like core.ts it only uses web-standard APIs and a small store interface, so it runs in Deno (Edge Function) and in Node (tests).
import { normalizeSerial, sha256Hex, timingSafeEqual } from "./core.ts";
import type { Result } from "./core.ts";

export interface GuitarRow {
  code: string;
  name: string;
  /** Rupiah, 0 = not set. */
  price: number;
  /** Percent 0-100. */
  promo: number;
}

export interface SerialRow {
  id: string;
  guitarCodes: string[];
  note: string;
  recipientEmail: string;
  /** ISO time. */
  createdAt: string;
  /** Email of the account that redeemed it, "" when unused (or when that account was deleted). */
  redeemedByEmail: string;
  /** ISO time, null while unused. */
  redeemedAt: string | null;
}

export type SerialStatus = "all" | "unused" | "used";

export interface AdminStore {
  /** SHA-256 hex of the shared secret of the 3ffect server, or null when it has not been set up. */
  getAdminSecretHash(): Promise<string | null>;
  listGuitars(): Promise<GuitarRow[]>;
  /** false when there is no such guitar. */
  setGuitarPrice(code: string, price: number, promo: number): Promise<boolean>;
  insertSerials(rows: { serialHash: string; guitarCodes: string[]; note: string; recipientEmail: string }[]): Promise<void>;
  listSerials(q: { status: SerialStatus; limit: number; offset: number }): Promise<{ rows: SerialRow[]; total: number }>;
  /** Only an unused serial can be deleted. */
  deleteUnusedSerial(id: string): Promise<"deleted" | "used" | "missing">;
  /** A one-time URL the browser can upload one file to (storage bucket "installers"), and the public URL the file will have. */
  createInstallerUpload(path: string): Promise<{ uploadUrl: string; token: string; publicUrl: string }>;
}

const ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no 0 O 1 I; 256 % 32 == 0, so no modulo bias
export const MAX_RECIPIENTS = 200;
export const MAX_COUNT = 500;
export const MAX_PRICE = 100_000_000;
/** The free Supabase plan refuses files over 50 MB; the bucket has the same limit. Bigger installers: host elsewhere and paste the link. */
export const MAX_INSTALLER_BYTES = 50 * 1024 * 1024;
const INSTALLER_EXT: Record<string, string[]> = { windows: ["exe", "msi", "zip"], macos: ["pkg", "dmg", "zip"] };

function fail(status: number, error: string, message: string): Result {
  return { status, body: { error, message } };
}

/** 20 random characters (100 bits) in 4 groups of 5, e.g. K7M4Q-2XB9D-RT6WH-P3NVC. Same format as tools/license/make_serials.py. */
export function newSerial(): string {
  const bytes = crypto.getRandomValues(new Uint8Array(20));
  const raw = [...bytes].map((b) => ALPHABET[b % 32]).join("");
  return [0, 5, 10, 15].map((i) => raw.slice(i, i + 5)).join("-");
}

const EMAIL = /^[^\s@<>"',;]+@[^\s@<>"',;]+\.[^\s@<>"',;]+$/;

/** Plain integer in [min, max]; numeric strings are accepted because HTML forms send strings. */
function intIn(v: unknown, min: number, max: number): number | null {
  const n = typeof v === "string" && /^\d+$/.test(v.trim()) ? Number(v.trim()) : v;
  return typeof n === "number" && Number.isInteger(n) && n >= min && n <= max ? n : null;
}

/**
 * POST admin-serials/<route>, header x-admin-secret. Routes:
 *   installer-upload-url { os: "windows"|"macos", version: "0.9.0", filename, size? }
 *                                                                       -> { uploadUrl, token, publicUrl, path, maxBytes }
 *   catalog        {}                                                   -> { guitars: [{code,name,price,promo}] }
 *   set-prices     { prices: [{code, price, promo}] }                   -> { guitars }
 *   create-serials { guitars: ["LP","STR"] | "ALL", recipients?: [email], count?: n, note? }
 *                                                                       -> { serials: [{ serial, recipientEmail, guitarCodes }] }
 *   list-serials   { status?: all|unused|used, limit?, offset? }        -> { rows: [...], total }
 *   delete-serial  { id }                                               -> { ok: true }
 * The plain serials are returned only by create-serials; the database keeps just their SHA-256.
 */
export async function adminRoute(store: AdminStore, secret: string, route: string, body: Record<string, unknown>): Promise<Result> {
  const expected = await store.getAdminSecretHash();
  if (!expected) return fail(503, "not_configured", "The admin API is not set up on this server.");
  if (!secret || !timingSafeEqual(expected, await sha256Hex(secret))) return fail(403, "forbidden", "Not allowed.");

  switch (route) {
    case "catalog":
      return { status: 200, body: { guitars: await store.listGuitars() } };
    case "set-prices":
      return await setPrices(store, body);
    case "create-serials":
      return await createSerials(store, body);
    case "list-serials":
      return await listSerials(store, body);
    case "delete-serial":
      return await deleteSerial(store, body);
    case "installer-upload-url":
      return await installerUploadUrl(store, body);
    default:
      return fail(404, "not_found", "Unknown route.");
  }
}

async function setPrices(store: AdminStore, body: Record<string, unknown>): Promise<Result> {
  const prices = body.prices;
  if (!Array.isArray(prices) || prices.length === 0 || prices.length > 50) return fail(400, "bad_request", "Send a list of prices.");
  const known = new Set((await store.listGuitars()).map((g) => g.code));
  const clean: { code: string; price: number; promo: number }[] = [];
  for (const p of prices) {
    const item = (p && typeof p === "object" ? p : {}) as Record<string, unknown>;
    const code = typeof item.code === "string" ? item.code.trim().toUpperCase() : "";
    const price = intIn(item.price, 0, MAX_PRICE);
    const promo = intIn(item.promo ?? 0, 0, 100);
    if (!known.has(code)) return fail(400, "unknown_guitar", `Unknown guitar "${code}".`);
    if (price === null) return fail(400, "bad_price", `Price of ${code} must be a whole number of Rupiah (0 to ${MAX_PRICE}).`);
    if (promo === null) return fail(400, "bad_promo", `Promo of ${code} must be a whole number from 0 to 100.`);
    clean.push({ code, price, promo });
  }
  // all validated first, so a bad row never leaves half of the prices changed
  for (const c of clean) await store.setGuitarPrice(c.code, c.price, c.promo);
  return { status: 200, body: { guitars: await store.listGuitars() } };
}

async function createSerials(store: AdminStore, body: Record<string, unknown>): Promise<Result> {
  const catalog = await store.listGuitars();
  const known = new Set(catalog.map((g) => g.code));

  let codes: string[];
  if (typeof body.guitars === "string" && body.guitars.trim().toUpperCase() === "ALL") {
    codes = catalog.map((g) => g.code);
  } else if (Array.isArray(body.guitars)) {
    codes = [...new Set(body.guitars.map((c) => (typeof c === "string" ? c.trim().toUpperCase() : "")))];
  } else {
    return fail(400, "bad_request", "Choose at least one guitar.");
  }
  if (codes.length === 0) return fail(400, "bad_request", "Choose at least one guitar.");
  const bad = codes.filter((c) => !known.has(c));
  if (bad.length) return fail(400, "unknown_guitar", `Unknown guitar: ${bad.map((c) => c || "(empty)").join(", ")}.`);

  const note = typeof body.note === "string" ? body.note.trim().slice(0, 200) : "";

  // one serial per recipient email, or `count` serials without a recipient
  let recipients: string[] = [];
  if (body.recipients !== undefined && body.recipients !== null) {
    if (!Array.isArray(body.recipients)) return fail(400, "bad_request", "recipients must be a list of emails.");
    const seen = new Set<string>();
    for (const r of body.recipients) {
      const email = typeof r === "string" ? r.trim().toLowerCase() : "";
      if (!email) continue;
      if (email.length > 254 || !EMAIL.test(email)) return fail(400, "bad_email", `"${String(r).slice(0, 80)}" is not a valid email address.`);
      if (!seen.has(email)) {
        seen.add(email);
        recipients.push(email);
      }
    }
    if (recipients.length > MAX_RECIPIENTS) return fail(400, "too_many", `At most ${MAX_RECIPIENTS} recipients at a time.`);
  }
  let count = recipients.length;
  if (count === 0) {
    const n = intIn(body.count ?? 1, 1, MAX_COUNT);
    if (n === null) return fail(400, "bad_count", `Count must be from 1 to ${MAX_COUNT}.`);
    count = n;
    recipients = [];
  }

  const made: { serial: string; recipientEmail: string; guitarCodes: string[] }[] = [];
  for (let i = 0; i < count; i++) made.push({ serial: newSerial(), recipientEmail: recipients[i] ?? "", guitarCodes: codes });
  await store.insertSerials(
    await Promise.all(
      made.map(async (m) => ({
        serialHash: await sha256Hex(normalizeSerial(m.serial)), // exactly what redeem-key hashes
        guitarCodes: m.guitarCodes,
        note,
        recipientEmail: m.recipientEmail,
      })),
    ),
  );
  return { status: 200, body: { serials: made } };
}

async function listSerials(store: AdminStore, body: Record<string, unknown>): Promise<Result> {
  const status = body.status === "unused" || body.status === "used" ? body.status : "all";
  const limit = intIn(body.limit ?? 50, 1, 200);
  const offset = intIn(body.offset ?? 0, 0, 1_000_000);
  if (limit === null || offset === null) return fail(400, "bad_request", "Invalid paging.");
  const { rows, total } = await store.listSerials({ status, limit, offset });
  // pass on only the known fields, so nothing extra (a hash, say) can ever leave through this route
  const safe = rows.map((r): SerialRow => ({
    id: r.id,
    guitarCodes: r.guitarCodes,
    note: r.note,
    recipientEmail: r.recipientEmail,
    createdAt: r.createdAt,
    redeemedByEmail: r.redeemedByEmail,
    redeemedAt: r.redeemedAt,
  }));
  return { status: 200, body: { rows: safe, total } };
}

async function deleteSerial(store: AdminStore, body: Record<string, unknown>): Promise<Result> {
  const id = body.id;
  if (typeof id !== "string" || !/^[0-9a-f-]{36}$/i.test(id)) return fail(400, "bad_request", "Invalid serial id.");
  const r = await store.deleteUnusedSerial(id);
  if (r === "missing") return fail(404, "not_found", "No such serial.");
  if (r === "used") return fail(409, "serial_used", "This serial was already redeemed, so it cannot be deleted. Take the guitar away from the account instead.");
  return { status: 200, body: { ok: true } };
}

/** Keeps letters, digits, dot, dash and underscore only, so a file name can never escape its folder. */
export function safeFileName(name: string): string {
  const base = name.split(/[\\/]/).pop() ?? "";
  return base.replace(/[^A-Za-z0-9._-]+/g, "-").replace(/^[.-]+/, "").slice(0, 120);
}

async function installerUploadUrl(store: AdminStore, body: Record<string, unknown>): Promise<Result> {
  const os = typeof body.os === "string" ? body.os.toLowerCase() : "";
  const allowed = INSTALLER_EXT[os];
  if (!allowed) return fail(400, "bad_os", "os must be windows or macos.");
  const version = typeof body.version === "string" ? body.version.trim() : "";
  if (!/^\d{1,3}\.\d{1,3}\.\d{1,3}(-[A-Za-z0-9.]{1,20})?$/.test(version)) return fail(400, "bad_version", "Version must look like 0.9.0.");
  const filename = typeof body.filename === "string" ? safeFileName(body.filename) : "";
  const ext = filename.includes(".") ? filename.split(".").pop()!.toLowerCase() : "";
  if (!filename || !allowed.includes(ext)) return fail(400, "bad_file", `A ${os} installer must be a .${allowed.join(", .")} file.`);
  if (body.size !== undefined && body.size !== null) {
    const size = typeof body.size === "number" ? body.size : NaN;
    if (!Number.isFinite(size) || size <= 0) return fail(400, "bad_size", "Invalid file size.");
    if (size > MAX_INSTALLER_BYTES) {
      return fail(413, "too_big", "This file is over 50 MB, which the free Supabase plan does not accept. Host it elsewhere (for example a GitHub Release) and paste the link.");
    }
  }
  const path = `${version}/${filename}`;
  const made = await store.createInstallerUpload(path);
  return { status: 200, body: { ...made, path, maxBytes: MAX_INSTALLER_BYTES } };
}
