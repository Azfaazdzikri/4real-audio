# Hasil analisis pertama data rekaman (13 Okt 2026)

Data: 6.953 file, 17 kombinasi gitar x posisi selector (8 gitar), tiap kombinasi 78 not sustain (6 senar x fret 0-12),
78 not palm mute, dan 253 file strum (chord). Fitur: spektrum 64 pita log (70 Hz - 19 kHz) per file, diekstrak
di Colab (`training/extract_features.py`), hasil di branch `results`.

## 1. Model fisik v0.3 vs rekaman nyata
Perbedaan spektrum nyata antar gitar (not yang sama, median 78 not) dibandingkan dengan prediksi model fisik:

| Ukuran (150 Hz - 8 kHz, 244 pasangan beda gitar) | Hasil |
|---|---|
| RMS perbedaan nyata ("tidak diubah" sebagai prediksi) | 4,16 dB |
| RMS error model fisik v0.3 | 4,13 dB |
| Pasangan yang model-nya lebih baik dari "tidak diubah" | 53% |
| Korelasi bentuk nyata vs model (median) | 0,16 |

Urutan kecerahan antar 17 kombinasi cukup benar (korelasi 0,62: Strat dan Tele paling cerah, Les Paul dan ES-335
paling gelap), tetapi rentang nyata sekitar 37 dB sedangkan model hanya sekitar 9 dB. Jadi elektronika pickup
saja (induktansi, DCR, pot, kapasitor) menjelaskan sebagian kecil perbedaan. Sisanya: lebar dan posisi pickup, senar,
bodi, tangan pemain, dan kondisi rekaman.

## 2. Seberapa stabil perbedaan itu
- Transfer yang dihitung dari fret genap vs fret ganjil berkorelasi 0,75: perbedaan antar gitar bersifat sistematis.
- Per not tunggal, error tanpa koreksi 6,83 dB dan dengan EQ tetap 6,26 dB: EQ tetap menjelaskan sedikit; variasi
  per not besar (struktur harmonik dan pengukuran per not yang berisik).

## 3. Mengenali gitar dari spektrum (kNN, not yang diuji tidak ikut latihan)
| Tugas | Akurasi | Peluang acak |
|---|---|---|
| 8 model gitar | 70,4% | 12,5% |
| 17 kombinasi gitar + posisi | 48,9% | 5,9% |

Les Paul 92%, Strat 84%, SG 73%; Gretsch 38% dan Rickenbacker 42% (satu posisi saja, data paling sedikit).
Peringatan: semua rekaman satu sesi, jadi sebagian akurasi bisa berasal dari kondisi sesi (senar, gain, tangan), bukan
dari gitarnya. Belum diuji pada gitar yang tidak ikut latihan.

## Arti untuk proyek
- Model fisik sendiri tidak cukup untuk "terdengar seperti gitar target". Pendekatan berbasis data (profil terukur) harus
  menjadi inti, model fisik hanya untuk perilaku knob.
- Deteksi otomatis gitar masuk akal sebagai fitur (akurasi jauh di atas acak), tetapi perlu data lebih banyak per gitar.
- Gitar pengguna (Strat HSS lokal) belum ada di data. Agar filter "sumber -> target" akurat, gitar itu perlu direkam
  dengan skrip yang sama (78 not sustain per posisi), atau profilnya diukur oleh plugin dari beberapa not yang dimainkan.
