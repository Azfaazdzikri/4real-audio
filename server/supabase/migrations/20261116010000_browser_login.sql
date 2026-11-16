-- Browser sign-in (device-code flow): the plugin starts a request, the player approves its code on the account site.
-- Written only by the Edge Functions (service role); players and the public API cannot touch these tables.

create table if not exists public.login_requests (
    id           uuid primary key default gen_random_uuid(),
    code         text not null unique,                 -- shown in the plugin, e.g. ABCD-EFGH
    secret_hash  text not null,                        -- sha-256 of the poll secret that only the plugin knows
    machine_id   text not null,
    machine_name text,
    status       text not null default 'pending' check (status in ('pending', 'approved', 'consumed')),
    user_id      uuid references auth.users (id) on delete cascade,
    created_at   timestamptz not null default now(),
    expires_at   timestamptz not null
);
create index if not exists login_requests_expires_idx on public.login_requests (expires_at);

-- Small server settings. 'approve_secret_sha256' = sha-256 hex of the secret the account site sends to browser-login/approve.
create table if not exists public.licence_settings (
    key   text primary key,
    value text not null
);

alter table public.login_requests   enable row level security;
alter table public.licence_settings enable row level security;
revoke all on public.login_requests, public.licence_settings from anon, authenticated;
