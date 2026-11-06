-- 4real-audio licensing: accounts come from Supabase Auth (auth.users). This adds what each account owns, the serial numbers
-- that unlock guitars, and the computers an account is signed in on.
-- Everything is written by the Edge Functions with the service role key. Players never write to these tables directly.

create table if not exists public.guitars (
    code text primary key,            -- the library code the plugin uses: LP, 335, SG, PRS, RIC, STR, TCR, FLC
    name text not null
);

insert into public.guitars (code, name) values
    ('LP',  'Les Paul'),
    ('335', 'ES-335'),
    ('SG',  'SG'),
    ('PRS', 'PRS SE Custom 24'),
    ('RIC', 'Rickenbacker 330'),
    ('STR', 'Stratocaster'),
    ('TCR', 'Telecaster'),
    ('FLC', 'Gretsch White Falcon')
on conflict (code) do nothing;

-- What an account may use. One row per account and guitar.
create table if not exists public.entitlements (
    user_id     uuid not null references auth.users (id) on delete cascade,
    guitar_code text not null references public.guitars (code),
    source      text not null default 'serial',   -- serial | manual | purchase
    created_at  timestamptz not null default now(),
    primary key (user_id, guitar_code)
);

-- Serial numbers. Only the SHA-256 of the normalised serial is stored (a leaked database does not leak usable serials).
-- One serial can unlock several guitars (a bundle) and can be redeemed once.
create table if not exists public.serials (
    id           uuid primary key default gen_random_uuid(),
    serial_hash  text not null unique,
    guitar_codes text[] not null check (cardinality(guitar_codes) > 0),
    note         text,                              -- e.g. the buyer or the order number, for your own records
    created_at   timestamptz not null default now(),
    redeemed_by  uuid references auth.users (id) on delete set null,
    redeemed_at  timestamptz
);

-- Computers an account is signed in on. The plugin keeps the device secret; only its hash is stored here.
create table if not exists public.devices (
    id           uuid primary key default gen_random_uuid(),
    user_id      uuid not null references auth.users (id) on delete cascade,
    machine_id   text not null,
    machine_name text,
    secret_hash  text not null,
    created_at   timestamptz not null default now(),
    last_seen_at timestamptz not null default now(),
    unique (user_id, machine_id)
);
create index if not exists devices_user_idx on public.devices (user_id);

-- Row level security: a signed-in player may READ their own rows (for a future website), nobody may write from the client.
alter table public.guitars       enable row level security;
alter table public.entitlements  enable row level security;
alter table public.serials       enable row level security;
alter table public.devices       enable row level security;

drop policy if exists guitars_read on public.guitars;
create policy guitars_read on public.guitars for select to anon, authenticated using (true);

drop policy if exists entitlements_read_own on public.entitlements;
create policy entitlements_read_own on public.entitlements for select to authenticated using (user_id = auth.uid());

drop policy if exists devices_read_own on public.devices;
create policy devices_read_own on public.devices for select to authenticated using (user_id = auth.uid());
-- (serials: no policy at all, so only the service role can touch it)

revoke all on public.serials from anon, authenticated;
revoke insert, update, delete on public.entitlements, public.devices, public.guitars from anon, authenticated;
revoke select (secret_hash) on public.devices from anon, authenticated;

-- Redeems a serial for an account in one atomic step. Returns the guitar codes it unlocked.
-- Raises 'invalid_serial' if no such serial, 'serial_used' if somebody already redeemed it.
create or replace function public.redeem_serial (p_serial_hash text, p_user_id uuid)
returns text[]
language plpgsql
security definer
set search_path = public
as $$
declare
    v_codes text[];
    v_row   public.serials%rowtype;
begin
    select * into v_row from public.serials where serial_hash = p_serial_hash for update;
    if not found then
        raise exception 'invalid_serial';
    end if;
    if v_row.redeemed_by is not null then
        raise exception 'serial_used';
    end if;

    update public.serials set redeemed_by = p_user_id, redeemed_at = now() where id = v_row.id;
    insert into public.entitlements (user_id, guitar_code, source)
        select p_user_id, c, 'serial' from unnest (v_row.guitar_codes) as c
        on conflict (user_id, guitar_code) do nothing;
    v_codes := v_row.guitar_codes;
    return v_codes;
end;
$$;

revoke all on function public.redeem_serial (text, uuid) from public, anon, authenticated;
grant execute on function public.redeem_serial (text, uuid) to service_role;
