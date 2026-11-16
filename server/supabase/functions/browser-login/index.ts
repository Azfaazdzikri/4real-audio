// Browser sign-in, three routes of one function (see "Browser sign-in" in _shared/core.ts):
//   POST /functions/v1/browser-login/start     { machineId, machineName }          -> { requestId, pollSecret, code, expiresIn, authorizeUrl }
//   POST /functions/v1/browser-login/poll      { requestId, pollSecret }           -> { status: "pending" } | { status: "approved", deviceId, deviceSecret, token, guitars }
//   POST /functions/v1/browser-login/approve   { code, email } + header x-approve-secret -> { ok: true, machineName }   (account site only)
import { browserLoginApprove, browserLoginPoll, browserLoginStart } from "./core.ts";
import { configFromEnv, respond, supabaseStore } from "./supabase_store.ts";

Deno.serve((req) =>
  respond(req, (r, body) => {
    const route = new URL(r.url).pathname.replace(/\/+$/, "").split("/").pop();
    const store = supabaseStore();
    const cfg = configFromEnv();
    if (route === "start") return browserLoginStart(store, cfg, body);
    if (route === "poll") return browserLoginPoll(store, cfg, body);
    if (route === "approve") return browserLoginApprove(store, cfg, r.headers.get("x-approve-secret") ?? "", body);
    return Promise.resolve({ status: 404, body: { error: "not_found", message: "Unknown route." } });
  })
);
