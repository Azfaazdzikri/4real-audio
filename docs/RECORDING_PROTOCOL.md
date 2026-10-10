# Protokol rekaman untuk data 4real-audio

Tujuan: rekaman gitar kering (DI) yang rapi dan konsisten, supaya bisa dipakai untuk
(1) mengkalibrasi model sirkuit gitar di `training/`, dan (2) nanti melatih neural network.

## Format file

- **WAV, bukan MP3.** MP3 membuang detail frekuensi tinggi dan menambah artefak kompresi, padahal
  detail itulah yang membedakan pickup. Model bisa ikut belajar artefak MP3.
- **24-bit (atau 32-bit float), 48 kHz, mono.** 96 atau 192 kHz tidak perlu: plugin berjalan di
  44,1/48 kHz dan isi suara gitar di bawah 20 kHz.
- Satu file per pengaturan (gitar, posisi selector, knob). Jangan campur beberapa pengaturan
  dalam satu file.

## Pengaturan tetap

- Input Hi-Z interface, kabel dan gain interface sama untuk semua rekaman. Puncak sinyal sekitar -12
  sampai -6 dBFS, tidak boleh clip.
- **Tanpa plugin di jalur sinyal** (matikan atau hapus UADx 1176 dan AmpliTube 5 di track), tanpa amp, tanpa mikrofon.
- Satu pick yang sama, posisi tangan pemetik dijaga sama (dekat bridge atau neck mengubah warna suara).
- Metronom 60 BPM di Studio One, satu not per ketukan, supaya rekaman antar gitar punya struktur waktu
  yang sama.
- Kecuali sedang menguji knob: volume 10, tone 10.

## Skrip rekaman (sekitar 25 menit per pengaturan)

Jangan strum sekali saja. Satu strum hanya memberi beberapa detik data. Rekam skrip berikut.

| # | Isi | Cara | Lama |
|---|---|---|---|
| 1 | Not tunggal | Tiap senar (6), fret 0 sampai 12 (13 posisi), tiap posisi dipetik pelan, sedang, dan keras. Satu not per ketukan, biarkan sustain sekitar 3 detik. Total 78 posisi x 3 kekuatan. | sekitar 16 menit |
| 2 | Harmonik alami | Fret 12, 7, dan 5 di tiap senar, masing-masing 2 petikan. | sekitar 2 menit |
| 3 | Chord | Chord terbuka (C, G, D, Am, Em) dan barre (F, B), masing-masing di-strum lalu dipetik per senar. | sekitar 3 menit |
| 4 | Permainan bebas | Riff, palm mute, hammer-on, slide, bend. Riff yang sama di semua pengaturan. | sekitar 5 menit |
| 5 | Hening | Volume gitar 10, tangan tidak menyentuh senar, 10 detik. Untuk mengenali dengung (hum). | 10 detik |

Catat not tunggal berdasarkan **senar dan fret**, bukan nama not (C, C#, D, ...). Nada yang sama di
senar berbeda terdengar berbeda, dan efek posisi pickup bergantung pada nada, jadi urutan senar dan
fret lebih penting daripada urutan nama not.

## Ulangi untuk tiap pengaturan

Ulangi skrip yang sama untuk setiap gitar dan setiap posisi selector yang ingin dipakai. Skrip,
tempo, dan pick yang sama membuat rekaman antar gitar bisa dibandingkan.

## Menyapu knob (untuk mencocokkan model sirkuit)

- Tone: satu chord sustain, putar tone dari 10 ke 0 perlahan, di satu posisi selector netral.
- Volume: satu chord sustain, putar volume dari 10 ke 0 perlahan.

## Ukur nilai listrik

- Hambatan DC (DCR) tiap pickup dengan multimeter. Kalau ada LCR meter, ukur juga induktansinya.
- Nilai pot (biasanya tertulis di badan pot) dan kapasitor tone kalau terbaca.
- Jenis dan panjang kabel. Jangan bongkar gitar kalau tidak nyaman melakukannya.

## Penamaan file dan catatan

`gitarA_pos1_vol10_tone10_notes-medium.wav`, `gitarA_pos1_vol10_tone10_chords.wav`,
`gitarA_pos1_vol10_tone10_riff.wav`, `gitarA_pos1_vol10_tone10_silence.wav`, dan seterusnya.
Taruh di `data/raw/` (tidak ikut git). Buat `notes.txt`: merek dan model gitar, pickup di tiap posisi,
DCR, pot, kabel, gain interface, usia senar.

## Seberapa banyak yang cukup

Ini aturan praktis, bukan angka pasti:

- Untuk kalibrasi kurva respons (tanpa neural network): 2 sampai 3 menit per pengaturan sudah memberi
  hasil stabil.
- Untuk pelatihan neural network: sedikitnya sekitar 10 sampai 20 menit per pengaturan sebagai titik awal,
  dan lebih banyak lebih baik. Jangkauan nada, kekuatan petikan, dan jenis permainan harus
  tercakup, bukan sekadar lama.

## Soal data berpasangan

Dua gitar yang direkam terpisah **tidak** menghasilkan pasangan yang sejajar per sampel, walaupun
nadanya sama. Rekaman seperti itu cocok untuk membandingkan kurva spektrum (sudah didukung
`training/transfer.py`), tetapi tidak cocok untuk melatih neural network dengan cara biasa.
Pasangan yang benar-benar sejajar hanya didapat kalau dua pickup direkam bersamaan dari gitar yang
sama (misalnya masing-masing ke satu kanal interface), atau lewat metode pelatihan tanpa pasangan.

## Ukuran file dan struktur folder `Data Training`

Struktur: `<gitar>/<posisi selector>/<Sustain | Palm Mute | Strum>/`. Isi dengan satu file per not.

- **Mono**, bukan stereo. Stereo dengan dua kanal sama persis memakai ruang dua kali lipat.
- **Sustain:** 6 sampai 8 detik per not (sekitar 1 MB per file di 24-bit/48 kHz).
- **Palm mute:** 2 sampai 3 detik per not (sekitar 0,4 MB).
- **Strum senar kosong:** 4 sampai 5 detik per file.
- Perkiraan: sekitar 0,6 GB per gitar untuk 400 not sustain + 400 not palm mute. Takaran 32 detik per not
  membuat file sekitar 15 kali lebih besar tanpa menambah informasi pickup.
- Penamaan yang disarankan: `S6_F00.wav` (senar 6, fret 0), `S1_F12.wav`; strum: `strum_E.wav`, `strum_A.wav`, dst.
- Cek folder dengan `python training/scan_dataset.py "Data Training"`: menghitung file dan durasi, dan menandai
  file stereo, sample rate selain 48 kHz, bit depth selain 24, dan take yang terlalu panjang.
