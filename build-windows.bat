@echo off
rem Distort Me - build the Photoshop plug-in on Windows.
rem Needs Visual Studio 2022 (with "Desktop development with C++") and CMake.
rem Usage:  build-windows.bat C:\path\to\adobe_photoshop_sdk_2026_win_v2

if "%~1"=="" (
  echo Usage: build-windows.bat ^<folder of the Photoshop SDK^>
  exit /b 1
)

cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 -DPHOTOSHOP_SDK_DIR="%~1" || exit /b 1
cmake --build build-vs --config Release || exit /b 1
ctest --test-dir build-vs -C Release || exit /b 1

echo.
echo Done: build-vs\Release\DistortMe.8bf
echo Copy it to C:\Program Files\Adobe\Adobe Photoshop 2026\Plug-ins\ and restart Photoshop.
