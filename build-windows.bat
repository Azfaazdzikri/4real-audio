@echo off
REM Build 4real-audio (VST3 + Standalone) on Windows.
REM Requirements: Visual Studio 2022 (or Build Tools) with "Desktop development with C++", CMake, Git.

cmake -S . -B build -A x64 || goto :error
cmake --build build --config Release --target FourRealAudio_VST3 FourRealAudio_Standalone --parallel || goto :error

echo.
echo Done. Output:
echo   build\FourRealAudio_artefacts\Release\VST3\4real-audio.vst3
echo   build\FourRealAudio_artefacts\Release\Standalone\4real-audio.exe
echo.
echo To use in Studio One, copy the whole "4real-audio.vst3" folder to:
echo   C:\Program Files\Common Files\VST3
pause
exit /b 0

:error
echo.
echo Build failed. Copy the error text above and send it back.
pause
exit /b 1
