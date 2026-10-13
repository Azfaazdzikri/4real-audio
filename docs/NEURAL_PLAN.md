# Rencana Neural Network 4real-audio

## Data (dari struktur folder Data Training)
17 sel (gitar x posisi pickup). Tiap sel: nada per senar (String 6..1) x {Sustain, Palm Mute}, plus Strumming (chord, open string).
Pasangan latih = **nada/chord yang sama** direkam di dua sel berbeda (tidak sample-aligned).

## Model
Conditional causal TCN (gaya WaveNet / NAM): dilasi 1..512, gated conv, FiLM dari embedding sel sumber + sel target,
keluaran residual yang dimulai sebagai identitas (awalnya "tanpa perubahan"). Receptive field ~2047 sampel (~43 ms).
Loss: multi-resolution STFT (512-4096) + loss spektrum rata-rata waktu (karena tidak sample-aligned).

## Evaluasi (nota/chord yang disisihkan, crc32(key)%100<20)
Error bentuk spektrum (dB) vs: (a) tanpa perubahan, (b) EQ statis dari data latih. NN harus mengalahkan keduanya.
Tes lanjutan: leave-one-cell-out (sel sumber yang tidak pernah dilihat) -> menentukan apakah gitar pengguna (Strat HSS lokal, tidak ada di data) bisa didukung.

## Risiko
Noise floor antar-take belum diketahui; satu sesi rekam; beberapa file clipping (dilewati); gitar HSS pengguna belum direkam.

## Integrasi plugin
Ekspor bobot (JSON) -> inferensi C++ di plugin (cond vector per pasangan sumber/target). Fallback: EQ statis terukur.

## Hasil v1 (20000 langkah, 462 pasangan uji)
Tanpa perubahan 5.70 dB | EQ statis 4.62 | NN 4.80. NN belum mengalahkan EQ statis (Sustain 6.31 vs 6.45 menang tipis;
Strum 4.02 vs 3.70 dan Palm Mute 9.21 vs 8.77 kalah). Error mendatar sejak langkah ~15000.

## v2
- Cabang EQ per pasangan (sumber, target) dengan 64 band, diinisialisasi dari EQ statis terukur -> model mulai di 4.62.
- Loss utama = metrik penilaian (RMS bentuk spektrum 64 band, 150-8000 Hz); STFT multi-resolusi jadi regulariser (0.3).
- Laporan baru: EQ statis per teknik (oracle) sebagai batas atas EQ sederhana.
- Kriteria lolos: model < EQ statis di Semua dan tidak lebih buruk dari "tanpa perubahan" di Palm Mute.
