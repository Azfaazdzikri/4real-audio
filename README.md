# 4real-audio

Plugin gitar (VST3 + Standalone) yang mengubah karakter suara satu gitar menjadi gitar lain
(misalnya Stratocaster single coil -> Les Paul humbucker) memakai neural network.
Outputnya tetap **clean** (tanpa amp/cab/pedal), jadi ditaruh sebelum AmpliTube 5 di insert chain:

```
Input gitar -> UADx 1176 -> [ 4real-audio ] -> AmpliTube 5 -> ...
```

## Status: v0.1 (test build)

- Audio masuk lalu keluar **tanpa diubah** (hanya ada knob Input / Output gain dan meter).
- Pilihan "Source guitar" dan "Target guitar" sudah tersimpan di project DAW, tapi **belum mengubah suara**.
- Model neural network belum ada. Versi ini hanya untuk memastikan plugin bisa dimuat dan dites di Studio One.
- Kode belum pernah dikompilasi penuh di Windows oleh pembuatnya (hanya dicek sintaksnya terhadap JUCE 9.0.3).
  Kalau build gagal, kirim teks error-nya.

## Cara mendapatkan file VST3 (Windows)

### Opsi A - GitHub Actions (tanpa install apa pun)
1. Push branch ini ke GitHub, buka tab **Actions**, pilih run terbaru "Build Windows (VST3 + Standalone)".
2. Unduh artifact `4real-audio-windows-x64` di bagian bawah halaman run.
3. Di dalamnya ada folder `4real-audio.vst3` dan `4real-audio.exe` (Standalone).

### Opsi B - build di PC sendiri
Butuh: Visual Studio 2022 (workload "Desktop development with C++"), CMake, Git.

```
git pull
build-windows.bat
```

Hasilnya ada di `build\FourRealAudio_artefacts\Release\VST3\4real-audio.vst3`.

## Mencoba di Studio One

1. Salin **seluruh folder** `4real-audio.vst3` ke `C:\Program Files\Common Files\VST3` (perlu hak admin),
   atau ke folder VST3 lain yang sudah ditambahkan di Studio One.
2. Buka Studio One, lakukan rescan plugin (di pengaturan lokasi VST plugin; nama menu bisa sedikit
   berbeda antar versi).
3. Taruh **4real-audio** di insert track gitar, di antara UADx 1176 dan AmpliTube 5.
4. Cek: suara lewat normal, knob Input/Output bekerja, meter bergerak, pengaturan tersimpan setelah project
   ditutup dan dibuka lagi.

## Kenapa tidak ada VST2 (.dll)?

Steinberg sudah menghentikan lisensi SDK VST2, jadi pengembang baru tidak bisa mendapatkannya dan
mendistribusikan plugin VST2 secara legal. Studio One membuka VST3 langsung, jadi VST3 dipakai untuk
pengujian. VST2 hanya mungkin kalau kamu sudah punya lisensi SDK VST2 lama.

## Rencana

Produk akhirnya satu plugin per gitar target: **4real-audio LP** (Les Paul: 3-way, 2 volume, 2 tone),
**STR** (Stratocaster: 5-way, 1 volume, 2 tone), **JZM** (Jazzmaster), **TCR** (Telecaster), dan
**335** (ES-335), masing-masing dengan UI bergaya gitar dan knob yang berfungsi seperti gitar aslinya.

1. **Prototipe model fisik (sudah ada, lihat `training/`):** model elektronika gitar (pickup, volume,
   tone, selector) dengan tes dan skrip pemrosesan WAV. Belum neural network.
2. **Rekam data dari Stratocaster HSS** sesuai `docs/RECORDING_PROTOCOL.md`, lalu kalibrasi nilai
   komponen model ke gitarmu.
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
