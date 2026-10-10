# 4real-audio

Plugin gitar (VST3 + Standalone) yang mengubah karakter suara satu gitar menjadi gitar lain
(misalnya Stratocaster single coil -> Les Paul humbucker) memakai neural network.
Outputnya tetap **clean** (tanpa amp/cab/pedal), jadi ditaruh sebelum AmpliTube 5 di insert chain:

```
Input gitar -> UADx 1176 -> [ 4real-audio ] -> AmpliTube 5 -> ...
```

## Status: v0.3 (test build, tanpa neural network)

Sekarang ada **8 plugin terpisah**, satu per gitar target, dari satu kode dasar:

| Plugin | Gitar target | Kontrol |
|---|---|---|
| 4real-audio LP | Les Paul | selector 3, 2 volume, 2 tone |
| 4real-audio 335 | ES-335 | selector 3, 2 volume, 2 tone |
| 4real-audio SG | SG | selector 3, 2 volume, 2 tone |
| 4real-audio PRS | PRS SE Custom 24 | selector 3, 1 volume, 1 tone |
| 4real-audio RIC | Rickenbacker 330 | selector 3, 2 volume, 2 tone |
| 4real-audio STR | Stratocaster SSS | selector 5, 1 volume, 2 tone |
| 4real-audio TCR | Telecaster | selector 3, 1 volume, 1 tone |
| 4real-audio FLC | Gretsch Falcon | selector 3, volume neck, volume bridge, master, 1 tone |

- Suara gitar difilter oleh **selisih dua model fisik elektronika gitar**: gitar sumber (pilih di dropdown:
  Strat HSS milikmu, atau salah satu gitar di tabel, lalu posisi selector) dan gitar target (plugin ini). Kontrol target
  bekerja seperti rangkaian aslinya (tone 0 menutup suara tinggi, volume rendah meredam resonansi pickup).
- Tombol **Voicing on** untuk A/B, dan **Keep level difference** untuk menyertakan atau menghilangkan selisih level.
- Ini **bukan suara gitar yang sebenarnya**: hanya bagian linear tetap dari pickup dan knob yang ditangkap.
  Efek posisi pickup yang bergantung nada, dinamika, dan karakter bodi/senar butuh model neural network.
  Nilai komponen adalah **perkiraan umum**, bukan hasil ukur (lihat `docs/GUITAR_MODELS.md`).
  Misalnya LP dan SG sekarang memakai nilai yang sama, jadi suaranya sama sampai ada data.
- Model fisiknya sama dengan prototipe Python di `training/` (hasil C++ dan Python dicek identik oleh
  `tests/check_parity.py`, model generik dicek terhadap rangkaian lama oleh `tests/dsp_test.cpp`).

## Cara mendapatkan file VST3 (Windows)

### Opsi A - GitHub Actions (tanpa install apa pun)
1. Push branch ini ke GitHub, buka tab **Actions**, pilih run terbaru "Build Windows (VST3 + Standalone)".
2. Unduh artifact `4real-audio-windows-x64` di bagian bawah halaman run (muncul setelah semua 8 build selesai).
3. Di dalamnya ada folder `4real-audio LP.vst3`, `4real-audio STR.vst3`, dst, dan file `.exe` Standalone masing-masing.

### Opsi B - build di PC sendiri
Butuh: Visual Studio 2022 (workload "Desktop development with C++"), CMake, Git.

```
git pull
build-windows.bat            (semua model)
build-windows.bat LP STR     (hanya model tertentu)
```

Hasilnya ada di `build\FourReal_LP_artefacts\Release\VST3\4real-audio LP.vst3` (dan seterusnya).

## Mencoba di Studio One

1. Salin **seluruh folder** `4real-audio XX.vst3` yang ingin dicoba ke `C:\Program Files\Common Files\VST3`
   (perlu hak admin), atau ke folder VST3 lain yang sudah ditambahkan di Studio One.
2. Buka Studio One, lakukan rescan plugin (di pengaturan lokasi VST plugin; nama menu bisa sedikit
   berbeda antar versi).
3. Taruh plugin di insert track gitar, di antara UADx 1176 dan AmpliTube 5.
4. Cek: suara lewat normal, knob bekerja sesuai gitarnya, pilihan "My guitar" dan posisi selector
   mengubah suara, meter bergerak, pengaturan tersimpan setelah project ditutup dan dibuka lagi.

## Kenapa tidak ada VST2 (.dll)?

Steinberg sudah menghentikan lisensi SDK VST2, jadi pengembang baru tidak bisa mendapatkannya dan
mendistribusikan plugin VST2 secara legal. Studio One membuka VST3 langsung, jadi VST3 dipakai untuk
pengujian. VST2 hanya mungkin kalau kamu sudah punya lisensi SDK VST2 lama.

## Rencana

Produk akhirnya satu plugin per gitar target (8 model di tabel di atas, Jazzmaster belum ada datanya),
masing-masing dengan UI bergaya gitar dan knob yang berfungsi seperti gitar aslinya.

1. **Prototipe model fisik (sudah ada, lihat `training/`):** model elektronika gitar (pickup, volume,
   tone, selector) dengan tes dan skrip pemrosesan WAV. Belum neural network.
2. **Rekam data** sesuai struktur folder `Data Training` dan `docs/RECORDING_PROTOCOL.md`
   (`python training/scan_dataset.py "Data Training"` memeriksa isi folder), lalu kalibrasi nilai
   komponen model ke gitar yang direkam.
3. **Neural network:** mempelajari sisanya (pengaruh posisi pickup yang bergantung pada nada, dinamika).
   Butuh data berpasangan atau metode tanpa pasangan.
4. **Inferensi real-time di plugin:** model kecil dan kausal (latensi rendah), dijalankan dengan RTNeural
   atau NAM core, disisipkan di `processBlock()` (lihat komentar TODO di `Source/PluginProcessor.cpp`).
5. **UI bergaya gitar** per model, dan deteksi otomatis jenis gitar dan posisi pickup input
   (awalnya dipilih manual lewat dropdown).

## Catatan data latih

Daftar dataset publik yang sudah dicek lisensinya ada di `docs/DATA_SOURCES.md`. Intinya: tidak ada
dataset publik berpasangan (performa sama, gitar berbeda), jadi rekaman gitar sendiri adalah data
inti. Capture amp seperti Tone3000 adalah model amp/pedal, bukan pickup. Tersedia publik bukan berarti
bebas dipakai, jadi cek lisensi dan catat sumbernya. Sandbox pembuatan kode ini tidak bisa mengakses
Hugging Face, jadi pengunduhan dataset dilakukan di PC kamu.

## Lisensi JUCE

Proyek ini memakai JUCE (AGPLv3 atau lisensi komersial). Kalau plugin akan didistribusikan sebagai
closed-source, perlu lisensi komersial JUCE. Kalau tidak, kodenya harus dirilis di bawah lisensi
yang kompatibel dengan AGPLv3.
