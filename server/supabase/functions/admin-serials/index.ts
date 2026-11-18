// Admin API for the 3ffect.my.id server (NOT for the plugin): serial numbers and guitar prices.
//   POST /functions/v1/admin-serials/catalog | set-prices | create-serials | list-serials | delete-serial
//   header x-admin-secret: the shared secret whose SHA-256 is stored in licence_settings ('admin_secret_sha256').
// Route details and bodies: see adminRoute() in _shared/admin.ts.
import { adminRoute } from "../_shared/admin.ts";
import { adminStore } from "../_shared/admin_store.ts";
import { respond } from "../_shared/supabase_store.ts";

Deno.serve((req) =>
  respond(req, (r, body) => {
    const route = new URL(r.url).pathname.replace(/\/+$/, "").split("/").pop() ?? "";
    return adminRoute(adminStore(), r.headers.get("x-admin-secret") ?? "", route, body);
  })
);
