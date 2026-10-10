@echo off
REM Build 4real-audio (VST3 + Standalone) on Windows. One plugin per guitar model.
REM Requirements: Visual Studio 2022 (or Build Tools) with "Desktop development with C++", CMake, Git.
REM Usage:  build-windows.bat            builds all models (LP 335 SG PRS RIC STR TCR FLC)
REM         build-windows.bat LP STR     builds only the listed models

set MODELS=%*
if "%MODELS%"=="" set MODELS=LP 335 SG PRS RIC STR TCR FLC

set CMAKE_MODELS=%MODELS: =;%
cmake -S . -B build -A x64 -DFOURREAL_MODELS="%CMAKE_MODELS%" || goto :error

for %%M in (%MODELS%) do (
    cmake --build build --config Release --target FourReal_%%M_VST3 FourReal_%%M_Standalone --parallel || goto :error
)

echo.
echo Done. Output per model, for example for LP:
echo   build\FourReal_LP_artefacts\Release\VST3\4real-audio LP.vst3
echo   build\FourReal_LP_artefacts\Release\Standalone\4real-audio LP.exe
echo.
echo To use in Studio One, copy the whole "4real-audio XX.vst3" folders to:
echo   C:\Program Files\Common Files\VST3
pause
exit /b 0

:error
echo.
echo Build failed. Copy the error text above and send it back.
pause
exit /b 1
