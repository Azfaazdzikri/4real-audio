# Sumber data untuk model 4real-audio

Dicek pada 9 Oktober 2026 lewat halaman resmi masing-masing dataset. Lisensi bisa berubah, jadi
**cek ulang halaman dataset sebelum dipakai** dan catat setiap dataset yang dipakai (nama, URL,
lisensi, tanggal unduh) di `data/MANIFEST.csv`.

## Kesimpulan utama

Tidak ada dataset publik yang saya temukan dan verifikasi yang berisi **performa yang sama**
direkam lewat gitar atau pickup yang berbeda. Padahal itulah data ideal untuk mengubah satu gitar
menjadi gitar lain. Jadi dataset publik hanya berguna sebagai data pendukung (contoh karakter
tiap jenis gitar, evaluasi, detektor jenis gitar). Data inti harus dari rekaman Stratocaster HSS
kamu sendiri (lihat `RECORDING_PROTOCOL.md`).

## Daftar dataset

| Dataset | Lisensi | Isi | Status untuk produk komersial |
|---|---|---|---|
| [Guitar-TECHS](https://guitar-techs.github.io) (unduh: [Zenodo 14963133](https://zenodo.org/records/14963133)) | CC BY 4.0 | Lebih dari 5 jam (05:12:02). Sinyal DI, amp dengan mikrofon, dan mikrofon ruangan dari 3 gitar: Ibanez Performer PF-300 (gaya Les Paul, pickup bridge), EVH Wolfgang Exotic (gaya Strat, pickup neck), Sire T7 (gaya Telecaster, bridge+neck). | Boleh dengan atribusi (kutip paper ICASSP 2025: Pedroza, Abreu, Corey, Roman). Pemain dan gitar berbeda, jadi **tidak berpasangan**. Gitarnya tiruan, bukan Gibson/Fender asli, dan tiap gitar hanya satu posisi pickup. |
| [IDMT-SMT-Guitar](https://zenodo.org/records/7544110) | CC BY-NC-ND 4.0 | 7 gitar, berbagai pengaturan pickup, 1,3 GB, mono 44,1 kHz. | **Jangan dipakai.** Non-komersial dan tanpa turunan. |
| [EGDB (render BIAS FX2)](https://zenodo.org/records/12658984) | Tidak tertulis di halaman | 1,0 GB, hasil render lewat plugin komersial. | Belum terverifikasi. Cek lisensi EGDB asli. Data hasil render plugin juga bisa terikat lisensi plugin. |
| EGFxSet ([Zenodo 7044411](https://zenodo.org/record/7044411)) | Tidak terbaca | Halamannya diblokir rate limit saat dicek. | Belum terverifikasi. |
| GADA (Guitar Audio Dataset for AI, AES Europe 2025) | Tidak dicek | Muncul di hasil pencarian sebagai korpus gitar open source. | Belum terverifikasi. |
| Tone3000 / capture NAM | Beda tiap capture | Model **amp dan pedal**, bukan rekaman gitar dengan pickup berbeda. | Tidak cocok untuk plugin ini (outputnya clean, tanpa amp). Cek lisensi tiap capture kalau suatu saat dipakai. |
| Hugging Face | Beda tiap dataset | Tidak bisa diakses dari sandbox pembuatan kode ini. | Cek lisensi di halaman dataset. |

## Paper yang relevan (ide, bukan data)

[EG-VAE (arXiv 2608.05513)](https://arxiv.org/pdf/2608.05513) membahas pemindahan nada gitar listrik
dengan memisahkan isi permainan dari karakter nada (tone). Dilatih dengan sinyal DI yang dirender
lewat preset plugin komersial, jadi yang dipindahkan adalah karakter amp dan efek, **bukan pickup**.
Rilis kode dan datanya tidak dikonfirmasi di paper. Idenya (isi vs nada, satu embedding nada) bisa
dipakai untuk tahap neural network nanti.

## Aturan pakai

1. Hanya pakai data dengan lisensi yang membolehkan penggunaan komersial dan pembuatan model.
2. Simpan atribusi yang diminta lisensi (CC BY wajib menyebut sumber).
3. Jangan memakai sample library komersial (termasuk Ample Guitar) sebagai data latih.
4. Rekaman sendiri paling aman, dan paling cocok dengan gitar yang kamu punya.
