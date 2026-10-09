# training/ - prototipe model gitar (tahap 1)

Prototipe ini **belum neural network**. Isinya model fisik elektronika gitar yang bisa dijalankan
dan diuji di Python:

- `guitar_circuit.py`: pickup (induktansi + DCR), pot volume, pot tone + kapasitor, selector, kabel.
  Ada rangkaian Stratocaster (5-way, 1 volume, 2 tone) dan Les Paul (3-way, 2 volume, 2 tone).
- `transfer.py`: membuat filter minimum-phase dari kurva respons, dan memperkirakan kurva transfer
  antara dua rekaman yang bukan performa yang sama.
- `process_wav.py`: mengubah rekaman DI gitar sumber menjadi terdengar seperti gitar target.
- `test_guitar_circuit.py`: 13 tes (resonansi, knob tone dan volume, selector, estimasi tanpa pasangan).

```
pip install -r training/requirements.txt
python training/test_guitar_circuit.py
python training/process_wav.py di.wav out.wav --source "strat-hss:position=1" --target "lp:selector=bridge" --plot curves.png
```

## Yang ditangkap dan yang belum

Ditangkap: puncak resonansi pickup, pengaruh kabel, cara knob volume/tone membebani pickup,
gabungan pickup paralel, selisih level antar pickup.

Belum: efek posisi dan lebar pickup yang bergantung pada nada, dinamika dan non-linearitas, dan
karakter bodi/senar. Bagian itu yang nanti dipelajari neural network, dan butuh data rekaman
(lihat `docs/RECORDING_PROTOCOL.md`).

Semua nilai komponen di preset adalah angka umum, bukan hasil ukur gitar tertentu. Ukur gitarmu
dan edit preset di `guitar_circuit.py`.
