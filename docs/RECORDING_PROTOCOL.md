# Protokol rekaman Stratocaster HSS

Tujuan: mendapatkan data dari gitarmu sendiri yang cukup untuk (1) mengkalibrasi model sirkuit gitar
di `training/`, dan (2) nanti melatih neural network. Semua rekaman adalah **DI kering**: tidak ada
amp, pedal, atau plugin di jalur sinyal.

## Pengaturan tetap (jangan diubah antar rekaman)

- Input Hi-Z audio interface yang sama, kabel yang sama, gain interface yang sama.
- 48 kHz, 24-bit atau 32-bit float. Puncak sinyal sekitar -12 dBFS, jangan sampai clip.
- Senar, tinggi pickup, dan posisi gitar tidak diubah selama sesi.
- Kecuali sedang menguji knob: volume 10, tone 10.
- Matikan semua insert di track gitar saat merekam (termasuk UADx 1176 dan AmpliTube 5).

## Yang direkam untuk setiap posisi selector (1 sampai 5)

Catat dulu pickup apa yang aktif di tiap posisi pada gitarmu. Pada HSS posisi 2 dan 4 bisa beda
antar gitar (misalnya humbucker di-split atau tidak), jadi tulis apa adanya.

1. **Not tunggal:** tiap senar, dari fret 0 sampai 12, dipetik pelan, sedang, dan keras. Biarkan
   sustain 3 sampai 4 detik.
2. **Chord terbuka:** dipetik dan di-strum.
3. **Riff:** 2 sampai 3 menit, riff yang **sama** di semua posisi (nada dan ritme semirip mungkin).
4. **Palm mute dan picking cepat:** sekitar 1 menit.

Target: minimal 10 menit per posisi. Makin banyak makin baik.

## Menyapu knob (untuk mencocokkan model sirkuit)

- Tone: satu chord sustain, putar tone dari 10 ke 0 perlahan, satu posisi selector netral (misalnya 5).
- Volume: satu chord sustain, putar volume dari 10 ke 0 perlahan.

## Ukur nilai listrik

- Hambatan DC (DCR) tiap pickup dengan multimeter. Kalau ada LCR meter, ukur juga induktansinya.
- Nilai pot (biasanya tertulis di badan pot, misalnya 250k) dan nilai kapasitor tone kalau terbaca.
- Jenis dan panjang kabel. Jangan bongkar gitar kalau tidak nyaman melakukannya.

## Penamaan file

`hss_pos1_vol10_tone10_take01.wav`, `hss_pos5_tonesweep_take01.wav`, dan seterusnya.
Buat satu file catatan `notes.txt` berisi posisi aktif tiap selector, DCR, pot, kabel, gain interface.

## Setelah direkam

1. Taruh file di `data/raw/` (folder ini tidak ikut git).
2. Jalankan `training/process_wav.py` untuk mendengar hasil model sirkuit pada rekamanmu.
3. Kirim ke saya ringkasan `notes.txt` dan beberapa file pendek (atau hasil analisis spektrumnya)
   supaya nilai induktansi dan kapasitansi di `training/guitar_circuit.py` bisa dikalibrasi ke
   gitarmu, bukan angka umum.
