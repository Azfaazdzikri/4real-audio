// POST /functions/v1/redeem-key   body: { deviceId, deviceSecret, machineId, serial }  ->  { token, guitars, added }
import { redeemKey } from "../_shared/core.ts";
import { configFromEnv, respond, supabaseStore } from "../_shared/supabase_store.ts";

Deno.serve((req) => respond(req, (_r, body) => redeemKey(supabaseStore(), configFromEnv(), body)));
