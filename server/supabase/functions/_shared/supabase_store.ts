// Store backed by Supabase (service role). Used by the Edge Functions only; the tests use an in-memory store.
import { createClient } from "npm:@supabase/supabase-js@2";
import type { Config, DeviceRow, Result, Store } from "./core.ts";

type Row = { id: string; user_id: string; machine_id: string; machine_name: string | null; secret_hash: string };
const toDevice = (r: Row): DeviceRow => ({
  id: r.id,
  userId: r.user_id,
  machineId: r.machine_id,
  machineName: r.machine_name ?? "",
  secretHash: r.secret_hash,
});

export function configFromEnv(): Config {
  const key = Deno.env.get("LICENSE_SIGNING_KEY");
  if (!key) throw new Error("secret LICENSE_SIGNING_KEY is not set");
  return {
    signingKeyPkcs8: key,
    maxDevices: Number(Deno.env.get("LICENSE_MAX_DEVICES") ?? "3"),
    tokenDays: Number(Deno.env.get("LICENSE_TOKEN_DAYS") ?? "30"),
  };
}

export function supabaseStore(): Store {
  const url = Deno.env.get("SUPABASE_URL")!;
  const serviceKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
  const db = createClient(url, serviceKey, { auth: { persistSession: false, autoRefreshToken: false } });

  const must = <T>(res: { data: T | null; error: { message: string } | null }): T => {
    if (res.error) throw new Error(res.error.message);
    return res.data as T;
  };

  return {
    async getUserFromJwt(jwt) {
      const { data, error } = await db.auth.getUser(jwt); // asks the Auth server, so it works with any JWT signing setup
      if (error || !data.user || !data.user.email) return null;
      return { id: data.user.id, email: data.user.email };
    },
    async signInWithPassword(email, password) {
      // The password is checked by the 3ffect.my.id account server (its own login, BCrypt and attempt limiter).
      // Its answer carries the canonical email. The licence account is a shadow user in Supabase Auth with that email,
      // created on first sign-in (so an existing 4real-audio account with the same email is simply reused).
      const base = (Deno.env.get("ACCOUNT_API_URL") ?? "https://threeffect-serverapp.onrender.com").replace(/\/+$/, "");
      let res: Response;
      try {
        // generous timeout: a free Render instance can need about a minute to wake up
        res = await fetch(`${base}/auth/login`, {
          method: "POST",
          headers: { "Content-Type": "application/json", Accept: "application/json" },
          body: JSON.stringify({ email, password }),
          signal: AbortSignal.timeout(100_000),
        });
      } catch (e) {
        console.error("account server unreachable:", e instanceof Error ? e.message : e);
        return { ok: false, reason: "unavailable" };
      }
      if (res.status === 401 || res.status === 400) return { ok: false, reason: "invalid" };
      if (res.status === 429) return { ok: false, reason: "throttled" };
      if (!res.ok) {
        console.error("account server answered", res.status);
        return { ok: false, reason: "unavailable" };
      }
      const info = (await res.json().catch(() => null)) as { email?: string } | null;
      const canonical = (typeof info?.email === "string" ? info.email : email).trim().toLowerCase();
      if (!canonical) return { ok: false, reason: "unavailable" };

      const lookup = async (): Promise<string | null> => {
        const { data, error } = await db.rpc("licence_user_id_for_email", { p_email: canonical });
        if (error) throw new Error(error.message);
        return (data as string | null) ?? null;
      };
      let id = await lookup();
      if (!id) {
        const created = await db.auth.admin.createUser({ email: canonical, email_confirm: true, password: crypto.randomUUID() + crypto.randomUUID() });
        if (created.error || !created.data.user) {
          id = await lookup(); // lost a race with a parallel sign-in
          if (!id) throw new Error(created.error?.message ?? "could not create the licence account");
        } else {
          id = created.data.user.id;
        }
      }
      return { ok: true, user: { id, email: canonical } };
    },
    async getUserEmail(userId) {
      const { data } = await db.auth.admin.getUserById(userId);
      return data.user?.email ?? "";
    },
    async getEntitlements(userId) {
      const rows = must(await db.from("entitlements").select("guitar_code").eq("user_id", userId));
      return (rows ?? []).map((r: { guitar_code: string }) => r.guitar_code);
    },
    async findDevice(userId, machineId) {
      const row = must(await db.from("devices").select("*").eq("user_id", userId).eq("machine_id", machineId).maybeSingle());
      return row ? toDevice(row as Row) : null;
    },
    async getDeviceById(id) {
      // an id that is not a uuid cannot match, and must not turn into a database error
      if (!/^[0-9a-f-]{36}$/i.test(id)) return null;
      const row = must(await db.from("devices").select("*").eq("id", id).maybeSingle());
      return row ? toDevice(row as Row) : null;
    },
    async countDevices(userId) {
      const res = await db.from("devices").select("id", { count: "exact", head: true }).eq("user_id", userId);
      if (res.error) throw new Error(res.error.message);
      return res.count ?? 0;
    },
    async upsertDevice(r) {
      const row = must(
        await db
          .from("devices")
          .upsert(
            { user_id: r.userId, machine_id: r.machineId, machine_name: r.machineName, secret_hash: r.secretHash, last_seen_at: new Date().toISOString() },
            { onConflict: "user_id,machine_id" },
          )
          .select("*")
          .single(),
      );
      return toDevice(row as Row);
    },
    async touchDevice(id) {
      await db.from("devices").update({ last_seen_at: new Date().toISOString() }).eq("id", id);
    },
    async deleteDevice(id) {
      await db.from("devices").delete().eq("id", id);
    },
    async redeemSerial(userId, serialHash) {
      const { data, error } = await db.rpc("redeem_serial", { p_serial_hash: serialHash, p_user_id: userId });
      if (error) {
        if (error.message.includes("serial_used")) return { ok: false, reason: "used" };
        if (error.message.includes("invalid_serial")) return { ok: false, reason: "invalid" };
        throw new Error(error.message);
      }
      return { ok: true, codes: data as string[] };
    },
  };
}

/** Shared wrapper of the four functions: JSON in, JSON out, errors never leak details. */
export async function respond(req: Request, handler: (req: Request, body: Record<string, unknown>) => Promise<Result>): Promise<Response> {
  const headers = { "Content-Type": "application/json" };
  if (req.method !== "POST") return new Response(JSON.stringify({ error: "method_not_allowed", message: "Use POST." }), { status: 405, headers });
  let body: Record<string, unknown> = {};
  try {
    const parsed = await req.json();
    if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) body = parsed as Record<string, unknown>;
  } catch {
    return new Response(JSON.stringify({ error: "bad_request", message: "Body must be JSON." }), { status: 400, headers });
  }
  try {
    const result = await handler(req, body);
    return new Response(JSON.stringify(result.body), { status: result.status, headers });
  } catch (e) {
    console.error("licence function failed:", e instanceof Error ? e.message : e);
    return new Response(JSON.stringify({ error: "server_error", message: "The licence server had a problem. Try again in a moment." }), { status: 500, headers });
  }
}
