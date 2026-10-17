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

## Keputusan (2026-10-13)
- Gitar HSS pengguna TIDAK akan direkam; hanya data 8 gitar yang dipakai. Sumber (input) dipilih dari 17 sel yang ada di data,
  jadi masalah "gitar sumber yang tak terlihat" tidak perlu diselesaikan dengan encoder.
- Gitar HSS pengguna sudah dimodifikasi (pot 500k, kapasitor tone 0.022 uF = sama dengan asumsi Les Paul).
  Untuk tes dipakai pickup humbucker bridge, dengan sumber = "Les Paul Bridge Humbucker".
  Ini pendekatan: bodi, skala, dan pickup tidak identik dengan Les Paul, jadi hasilnya pendekatan, bukan pencocokan tepat.
- Format model: .ngm (Neural Guitar Modeler), dimodifikasi dari .nam: JSON berisi konfigurasi arsitektur, bobot, metadata (target, sel/posisi, sample rate, kalibrasi level).

## v0.5 (2026-10-14): neural stage in the plugin

- `.ngm` files (one per target guitar) live in `models/<CODE>/model.ngm` and are embedded in each plugin at build time (`NgmData::model_ngm`).
- C++ engine: `Source/dsp/NgmModel.*` (loader), `Source/dsp/NgmEngine.*` (streaming TCN, SSE2). Matches PyTorch to about -88 dB on the golden vectors (`tests/ngm_test.cpp`, `tests/golden/`). Cost about 16% of one core per channel (2.1 GHz cloud Xeon); a mono guitar on a stereo bus is computed once.
- Signal path: input gain -> auto level (slow RMS to -30 dB, undone after the net) -> per-pair EQ (min-phase FIR) -> TCN -> knob filter (physical model, knobs now vs full) -> output gain.
- Known limits: trained at 48 kHz (other rates: EQ exact, neural detail approximate; resampling not implemented); the EQ is min-phase in the plugin but zero-phase in training (about 15-20 dB below the signal difference on the golden test, a phase difference); Palm Mute is still the weakest technique; held-out test comes from the same recording session.

## v3 (2026-10-16): spectral-envelope model (replaces the waveform TCN)

Why: the v2 TCN generates waveform. Training pairs are not sample aligned (different performances), so the loss can only compare
spectra, and a spectrum-only loss lets a waveform generator add harmonics and texture. Measured on the v2 `.ngm`: a pure
220 Hz sine at training level comes out with about -11 dB harmonic distortion (about 28%), and the network residual is
50-60% of the signal amplitude. In a plugin this is audible as crackle. NAM does not have this problem because it trains on
sample-aligned input/output (a re-amped DI) with a waveform loss; this data cannot be aligned.

v3: the network never touches the waveform. Every 512 samples (10.7 ms) it looks at the band spectra of the last ~0.3 s of the
INPUT, and predicts a gain curve (64 bands, dB) on top of the static per-pair EQ. The plugin applies it as a minimum-phase FIR, so
the output is the input filtered (no added distortion or noise by construction). Cost is a fraction of a percent of a core.

- Data: `prepare_frames.py` stores only per-frame band spectra (4096-sample frames, hop 512): Sustain/Strum 8 s, Palm Mute 4 s, mono. About 0.7 GB.
  Also reports the left/right channel difference (mono vs stereo question).
- Loss: the evaluation metric (shape-only RMS over the 64 bands) on the whole clip + on 170 ms segments (decay / palm mute) + smoothness.
- Limits that remain: a spectral-envelope model can only change tone (EQ-like, time varying). It cannot reproduce pick attack, string excitation
  or other details that need aligned data. The dynamic part is smooth (20 control points), so pitch-dependent comb structure of a pickup
  position is only partly captured.
- Tools: `train_spec.py` (train + held-out evaluation), `listen.py` (writes source / static / model / target wavs for A/B listening and checks that
  the filtered audio scores like the training evaluation), `spec_numpy.py` (exact reference of the plugin algorithm, to port to C++),
  notebook `colab_stage4_spec.ipynb`.

## v0.6.0 - spectral model in the plugin (v3 model, v4 tried and dropped)

- v2 (waveform TCN) is removed from the plugin: it generated harmonics (about -11 dB distortion on a sine) because it was trained
  on unaligned pairs with a spectral loss only.
- The plugin now runs the v3 spectral model (`results-spec2`, tapered static EQ, shape error 7.25 -> 4.49 (static EQ) -> 4.14 (model)
  on 462 held-out pairs, 64 bands, 100 Hz - 16 kHz): per hop of 512 samples, the last 4096 samples -> 64-band spectrum -> small
  causal conv net -> dynamic correction + static per-pair EQ -> 1024-tap minimum-phase FIR, crossfaded over the hop.
  The output is always the input filtered: no distortion, no added noise, zero latency. About 7 % of one core per channel.
- `training/nn/export_spec.py model.pt models/` writes `models/<CODE>/model.spm` (JSON, embedded in the plugin as `SpmData::model_spm`)
  and golden vectors `tests/golden/golden_spm_<CODE>.json`; `tests/spm_test.cpp` checks the C++ engine against the numpy reference
  (`spec_numpy.py`): the filtered audio matches to about -133 dB, the gain curves to 0.0000 dB.
- Plugin parameter `intensity` (0-150 %, default 100) multiplies the gain curve before the filter design. Choosing the target's own
  pickup as the source gives intensity 0 (no change).
- v4 (162 fine bands + across-frequency conv net, `FOURREAL_BANDS=hi`, `train_spec.py --band-ch 8`) gave no gain: 4.08 vs 4.13 on the
  same 64-band metric after 3.3 h of CPU training. Fine harmonic detail does not generalise from unaligned pairs.
- Next idea: physics-informed model (pickup position comb filter, aperture, resonance/loading) fitted to the data for single notes,
  plus aligned data (two pickups recorded at once, or a repeatable string excitation) for waveform-level modelling.
