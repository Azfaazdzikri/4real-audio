-- Admin tab on 3ffect.my.id: manage the guitar prices and see who each serial was made for.
-- Everything here is written only by the admin-serials Edge Function (service role), called by the 3ffect server.

-- Selling price per guitar in Rupiah (0 = not set yet) and a promo discount in percent (0-100), same idea as the 3ffect shop.
alter table public.guitars add column if not exists price bigint  not null default 0 check (price >= 0);
alter table public.guitars add column if not exists promo integer not null default 0 check (promo between 0 and 100);

-- Who a serial was made for (the email the admin sent it to). Only for the admin's own records; redeeming does not depend on it.
alter table public.serials add column if not exists recipient_email text;

create index if not exists serials_created_idx on public.serials (created_at desc);

-- Shared secret of the 3ffect server for the admin-serials function: store its SHA-256 (hex) here, e.g.
--   insert into public.licence_settings (key, value)
--   values ('admin_secret_sha256', encode(sha256('<your long random secret>'::bytea), 'hex'))
--   on conflict (key) do update set value = excluded.value;
-- and give the same secret to the 3ffect server as FOURREAL_ADMIN_SECRET. (licence_settings is not readable by players.)
