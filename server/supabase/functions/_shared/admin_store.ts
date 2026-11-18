// Supabase (service role) implementation of AdminStore. Used by the admin-serials Edge Function only.
import { createClient } from "npm:@supabase/supabase-js@2";
import type { AdminStore, GuitarRow, SerialRow } from "./admin.ts";

type SerialDb = {
  id: string;
  guitar_codes: string[];
  note: string | null;
  recipient_email: string | null;
  created_at: string;
  redeemed_by: string | null;
  redeemed_at: string | null;
};

export function adminStore(): AdminStore {
  const url = Deno.env.get("SUPABASE_URL")!;
  const serviceKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!;
  const db = createClient(url, serviceKey, { auth: { persistSession: false, autoRefreshToken: false } });

  const must = <T>(res: { data: T | null; error: { message: string } | null }): T => {
    if (res.error) throw new Error(res.error.message);
    return res.data as T;
  };

  return {
    async getAdminSecretHash() {
      const row = must(await db.from("licence_settings").select("value").eq("key", "admin_secret_sha256").maybeSingle());
      return (row as { value: string } | null)?.value ?? null;
    },
    async listGuitars() {
      const rows = must(await db.from("guitars").select("code,name,price,promo").order("code"));
      // keep the plugin's own order (LP, 335, SG, ...) rather than alphabetical
      const order = ["LP", "335", "SG", "PRS", "RIC", "STR", "TCR", "FLC"];
      const list = ((rows ?? []) as { code: string; name: string; price: number | string; promo: number }[]).map((r): GuitarRow => ({
        code: r.code,
        name: r.name,
        price: Number(r.price),
        promo: r.promo,
      }));
      const rank = (c: string) => (order.indexOf(c) === -1 ? order.length : order.indexOf(c));
      return list.sort((a, b) => rank(a.code) - rank(b.code) || a.code.localeCompare(b.code));
    },
    async setGuitarPrice(code, price, promo) {
      const { data, error } = await db.from("guitars").update({ price, promo }).eq("code", code).select("code");
      if (error) throw new Error(error.message);
      return (data ?? []).length > 0;
    },
    async insertSerials(rows) {
      const { error } = await db.from("serials").insert(
        rows.map((r) => ({ serial_hash: r.serialHash, guitar_codes: r.guitarCodes, note: r.note || null, recipient_email: r.recipientEmail || null })),
      );
      if (error) throw new Error(error.message);
    },
    async listSerials({ status, limit, offset }) {
      let q = db
        .from("serials")
        .select("id,guitar_codes,note,recipient_email,created_at,redeemed_by,redeemed_at", { count: "exact" })
        .order("created_at", { ascending: false })
        .order("id", { ascending: false })
        .range(offset, offset + limit - 1);
      if (status === "unused") q = q.is("redeemed_at", null);
      if (status === "used") q = q.not("redeemed_at", "is", null);
      const res = await q;
      if (res.error) throw new Error(res.error.message);
      const data = (res.data ?? []) as SerialDb[];

      // emails of the accounts that redeemed (auth.users is only reachable through the admin API)
      const emails = new Map<string, string>();
      for (const id of new Set(data.map((r) => r.redeemed_by).filter((x): x is string => !!x))) {
        const { data: u } = await db.auth.admin.getUserById(id);
        emails.set(id, u.user?.email ?? "");
      }
      const rows = data.map((r): SerialRow => ({
        id: r.id,
        guitarCodes: r.guitar_codes,
        note: r.note ?? "",
        recipientEmail: r.recipient_email ?? "",
        createdAt: r.created_at,
        redeemedByEmail: r.redeemed_by ? (emails.get(r.redeemed_by) ?? "") : "",
        redeemedAt: r.redeemed_at,
      }));
      return { rows, total: res.count ?? rows.length };
    },
    async deleteUnusedSerial(id) {
      const { data, error } = await db.from("serials").delete().eq("id", id).is("redeemed_at", null).select("id");
      if (error) throw new Error(error.message);
      if ((data ?? []).length > 0) return "deleted";
      const existing = must(await db.from("serials").select("id").eq("id", id).maybeSingle());
      return existing ? "used" : "missing";
    },
  };
}
