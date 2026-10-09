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

1. **Prototipe Python/PyTorch:** kumpulkan data, latih model satu pasangan gitar, evaluasi dengan telinga
   dan spektrum.
2. **Inferensi real-time di plugin:** model kecil dan kausal (latensi rendah), dijalankan dengan RTNeural
   atau NAM core, disisipkan di `processBlock()` (lihat komentar TODO di `Source/PluginProcessor.cpp`).
3. **UI bergaya Ample Guitar:** gambar gitar dan knob yang sesuai gitar aslinya.
4. Deteksi otomatis jenis gitar input sebagai fitur tambahan (awalnya dipilih manual lewat dropdown).

## Catatan data latih

- Capture amp (misalnya di Tone3000) berisi model **amp/pedal**, bukan rekaman gitar dengan pickup
  berbeda, jadi tidak cocok sebagai data utama untuk mengubah gitar menjadi gitar lain.
- Tersedia secara publik bukan berarti bebas dipakai: tanpa lisensi tertulis, hak ciptanya tetap pada
  pembuat. Pakai rekaman sendiri atau dataset dengan lisensi yang jelas, dan catat sumber serta
  lisensinya.
- Sandbox tempat kode ini dibuat tidak bisa mengakses Hugging Face, jadi pengunduhan dataset sebaiknya
  dilakukan di PC kamu.

## Lisensi JUCE

Proyek ini memakai JUCE (AGPLv3 atau lisensi komersial). Kalau plugin akan didistribusikan sebagai
closed-source, perlu lisensi komersial JUCE. Kalau tidak, kodenya harus dirilis di bawah lisensi
yang kompatibel dengan AGPLv3.
