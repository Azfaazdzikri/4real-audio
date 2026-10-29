# 4real-audio

Plugin gitar (VST3 + Standalone) yang mengubah karakter suara satu gitar menjadi gitar lain
(misalnya Stratocaster single coil -> Les Paul humbucker) memakai neural network.
Outputnya tetap **clean** (tanpa amp/cab/pedal), jadi ditaruh sebelum AmpliTube 5 di insert chain:

```
Input gitar -> UADx 1176 -> [ 4real-audio ] -> AmpliTube 5 -> ...
```

## Status: v0.9 (satu plugin, library gitar)

Sekarang hanya **satu plugin / standalone** (`4real-audio`) yang berisi seluruh library gitar. Gitar yang ditiru dipilih
di daftar kartu sebelah kiri (seperti ganti library piano di Pianoverse); satu klik langsung pindah gitar tanpa bunyi klik.

| Kode | Gitar target | Kontrol di foto |
|---|---|---|
| LP | LP | selector 3, 2 volume, 2 tone |
| 335 | 335 | selector (hanya Neck yang direkam), 2 volume, 2 tone |
| SG | SG | selector (Bridge/Neck), 2 volume, 2 tone |
| PRS | SE24 | selector 3, 1 volume, 1 tone |
| RIC | Rickenbacker 330 | selector (hanya Bridge yang direkam), 2 volume, 2 tone |
| STR | Stratocaster SSS | selector 5 posisi (4 direkam), 1 volume, 2 tone |
| TCR | Telecaster | selector (Bridge/Neck), 1 volume, 1 tone |
| FLC | Gretsch-style Falcon | selector (Middle direkam), 2 volume, master, 1 tone |

- Tiap gitar tampil sebagai **foto** dengan kontrol yang hidup: klik pickup untuk memilihnya, geser tuas selector, putar knob
  volume/tone (knob digambar sendiri oleh plugin dan benar-benar berputar) (drag naik-turun, scroll mouse, double-click = 10, Shift = halus). Knob hanya menunjukkan angka 0.0 - 10.0.
  Posisi selector yang tidak direkam tidak bisa dipilih. Setiap gitar mengingat posisi knob-nya sendiri.
- **Lisensi per gitar**: `Source/License.h` sudah punya id lisensi per gitar (`4real.guitar.<KODE>`), tapi untuk sekarang
  semuanya terbuka (`kUnlockEverything = true`).
- Foto gitar (`Source/ui/photos/<KODE>.jpg`, 1376x768) adalah foto bersih tanpa knob dan tanpa tuas selector; knob, tuas, dan
  pickup menyala digambar oleh plugin. Posisi pickup/knob/tuas di foto ada di `Source/ui/GuitarScene.cpp` (satuan piksel foto);
  `tools/scene_preview.cpp` merender hasilnya ke PNG tanpa GUI. Kalau foto diganti, sesuaikan angka di file itu, lalu jalankan
  `tools/render_art.cpp` lagi.
- Gitar adalah gambar vektor flat (asli di `Source/ui/vector`, `tools/make_backgrounds.py` menaruhnya di latar kayu -> `Source/ui/photos`; tanpa knob dan selector). Knob dan tuas digambar dengan kode (`Source/ui/GuitarScene.cpp`);
  `tools/render_art.cpp` merender gambar per posisi selector dan filmstrip knob ke `Source/ui/art`.
- Suara gitar difilter oleh **selisih dua model fisik elektronika gitar**: gitar sumber (pilih di dropdown salah satu
  gitar yang direkam, lalu posisi selector; default Les Paul / Bridge, pendekatan untuk gitar HSS dengan humbucker bridge) dan gitar target (plugin ini). Kontrol target
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
2. Unduh artifact `4real-audio-windows-x64` di bagian bawah halaman run (muncul setelah build selesai).
3. Di dalamnya ada folder `4real-audio.vst3` dan `4real-audio.exe` (Standalone).

### Opsi B - build di PC sendiri
Butuh: Visual Studio 2022 (workload "Desktop development with C++"), CMake, Git.

```
git pull
build-windows.bat
```

Hasilnya ada di `build\FourReal_artefacts\Release\VST3\4real-audio.vst3`.

## Mencoba di Studio One

1. Salin **seluruh folder** `4real-audio.vst3` ke `C:\Program Files\Common Files\VST3`
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

Produk akhirnya satu plugin dengan library gitar (8 model di tabel di atas, Jazzmaster belum ada datanya),
masing-masing dengan UI foto gitar dan knob yang berfungsi seperti gitar aslinya.

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
