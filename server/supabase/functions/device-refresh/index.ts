// POST /functions/v1/device-refresh   body: { deviceId, deviceSecret, machineId }  ->  { token, guitars }
import { deviceRefresh } from "../_shared/core.ts";
import { configFromEnv, respond, supabaseStore } from "../_shared/supabase_store.ts";

Deno.serve((req) => respond(req, (_r, body) => deviceRefresh(supabaseStore(), configFromEnv(), body)));
