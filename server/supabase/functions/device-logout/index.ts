// POST /functions/v1/device-logout   body: { deviceId, deviceSecret }  ->  { ok: true }
import { deviceLogout } from "../_shared/core.ts";
import { respond, supabaseStore } from "../_shared/supabase_store.ts";

Deno.serve((req) => respond(req, (_r, body) => deviceLogout(supabaseStore(), body)));
