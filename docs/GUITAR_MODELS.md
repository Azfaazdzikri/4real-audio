# Model gitar di plugin (v0.3)

Semua nilai di bawah adalah **perkiraan umum** untuk keluarga gitar tersebut, bukan hasil ukur gitar tertentu.
Tujuannya agar kontrol (selector, volume, tone) berperilaku masuk akal. Ukur DCR dan induktansi pickup, nilai pot,
dan kapasitor tone dari gitar yang direkam, lalu ganti angka di `Source/dsp/GuitarModels.cpp`.

| Plugin | Pickup (induktansi / DCR) | Pot | Kapasitor | Kontrol |
|---|---|---|---|---|
| LP | bridge 5,5 H / 8,5k; neck 4,5 H / 7,5k | 500k | 22 nF | volume + tone per pickup |
| 335 | bridge 5,2 H / 8,2k; neck 4,5 H / 7,5k | 500k | 22 nF | volume + tone per pickup |
| SG | sama dengan LP (asumsi sampai diukur) | 500k | 22 nF | volume + tone per pickup |
| PRS | bridge 4,9 H / 8,6k; neck 4,3 H / 7,6k | 500k | 22 nF | 1 volume, 1 tone |
| RIC | bridge 3,3 H / 8,0k; neck 3,0 H / 7,2k | 250k | 22 nF | volume + tone per pickup |
| STR | bridge 2,6 H / 6,6k; tengah 2,3 H / 5,9k; neck 2,2 H / 5,8k | 250k | 22 nF | 1 volume; tone neck, tone tengah |
| TCR | bridge 2,8 H / 7,6k; neck 2,5 H / 6,8k | 250k | 47 nF | 1 volume, 1 tone |
| FLC | bridge 3,0 H / 4,9k; neck 2,8 H / 4,6k (Filter'Tron) | 500k | 22 nF | volume neck, volume bridge, master, 1 tone |
| Strat HSS (sumber) | bridge humbucker 4,0 H / 9,0k; tengah 2,3 H / 5,9k; neck 2,2 H / 5,8k | 250k | 22 nF | 1 volume, 2 tone |

Yang belum dimodelkan: saklar tone tiga posisi Gretsch, coil split, wiring khusus, kabel gitar yang berbeda
(default 500 pF). Efek bodi, kayu, senar, dan posisi pickup yang bergantung nada tidak ada di model fisik ini;
itu tugas tahap neural network.

Folder `Data Training` yang kamu kirim cocok dengan model ini: LP, 335, SG, PRS, RIC, STR, TCR, FLC. Jazzmaster
tidak ada di folder, jadi belum dibuat.
