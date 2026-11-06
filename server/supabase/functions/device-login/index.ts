// POST /functions/v1/device-login   Authorization: Bearer <access token of the signed-in account>
// body: { machineId, machineName }  ->  { deviceId, deviceSecret, token, guitars }
import { deviceLogin } from "../_shared/core.ts";
import { configFromEnv, respond, supabaseStore } from "../_shared/supabase_store.ts";

Deno.serve((req) =>
  respond(req, (r, body) => deviceLogin(supabaseStore(), configFromEnv(), (r.headers.get("Authorization") ?? "").replace(/^Bearer\s+/i, ""), body))
);
