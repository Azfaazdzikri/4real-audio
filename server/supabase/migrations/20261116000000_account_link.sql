-- Accounts now come from the 3ffect.my.id account server. A licence account is a "shadow" Supabase Auth user with the same email,
-- created by the device-login function the first time that person signs in. An older 4real-audio account with the same email
-- is reused as it is, so its guitars stay.
-- This helper finds the shadow user by email (auth.users is not readable through the API).

create or replace function public.licence_user_id_for_email (p_email text)
returns uuid
language sql
stable
security definer
set search_path = public, auth
as $$
    select id from auth.users where lower(email) = lower(p_email) limit 1;
$$;

revoke all on function public.licence_user_id_for_email (text) from public, anon, authenticated;
grant execute on function public.licence_user_id_for_email (text) to service_role;
