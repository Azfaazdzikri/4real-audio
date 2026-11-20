-- Public bucket for the Windows / macOS installers. Anyone may download; only the admin-serials function (service role) can
-- create upload URLs, so there is deliberately no policy that allows writing. The free plan caps files at 50 MB.
insert into storage.buckets (id, name, public, file_size_limit)
values ('installers', 'installers', true, 52428800)
on conflict (id) do nothing;
