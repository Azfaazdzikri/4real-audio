# Installer 4real-audio

GitHub Actions (`.github/workflows/build.yml`) membuat satu installer per sistem operasi. Unduh dari tab **Actions** → run terbaru → **Artifacts**
(GitHub membungkus file dalam satu ZIP; buka ZIP itu sekali, hasilnya sudah installer-nya):

| Sistem | File | Isi |
|---|---|---|
| Windows 64-bit | `4real-audio-<versi>-Windows-x64-Setup.exe` | VST3 → `C:\Program Files\Common Files\VST3`, aplikasi standalone → `C:\Program Files\4real-audio` + Start menu, uninstaller di Settings → Apps |
| macOS 11+ | `4real-audio-<versi>-macOS-AppleSilicon.pkg` (Mac M1/M2/M3/M4) dan `4real-audio-<versi>-macOS-Intel.pkg` (Mac Intel), masing-masing di bawah 50 MB | VST3 → `/Library/Audio/Plug-Ins/VST3`, Audio Unit → `/Library/Audio/Plug-Ins/Components`, standalone → `/Applications` |

Pemain tidak perlu memindahkan file: jalankan installer, pilih bagian yang diinginkan, selesai.

Semua build menerapkan lisensi (tidak ada versi unlocked). Versi diambil dari `project(... VERSION x.y.z)` di `CMakeLists.txt`.

## Peringatan keamanan OS (installer belum ditandatangani)

- **Windows SmartScreen** menampilkan "Windows protected your PC" untuk installer yang belum ditandatangani: klik **More info → Run anyway**.
  Untuk menghilangkannya perlu sertifikat code signing (berbayar).
- **macOS Gatekeeper** menolak membuka `.pkg` yang belum dinotarisasi: klik kanan file → **Open**, atau **System Settings → Privacy & Security → Open Anyway**.
  Plugin ditandatangani ad-hoc supaya bisa dimuat di Apple Silicon. Untuk menghilangkan peringatan perlu Apple Developer ID (berbayar) dan notarisasi.

## Membangun di komputer sendiri

- Windows: `build-windows.bat` lalu `iscc /DAppVersion=0.9.0 /DBuildDir=<path build\FourReal_artefacts\Release> installer\windows\4real-audio.iss` (Inno Setup 6.3+).
- macOS: `cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`, build target
  `FourReal_VST3 FourReal_AU FourReal_Standalone` (config Release), lalu `installer/macos/build_pkg.sh 0.9.0 build/FourReal_artefacts/Release out`.
