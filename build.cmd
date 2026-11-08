@echo off
setlocal EnableExtensions
cd /d "%~dp0"

REM MSYS2 UCRT64. При нестандартном расположении задайте MSYS2_ROOT.
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "UCRT_BIN=%MSYS2_ROOT%\ucrt64\bin"
if exist "%UCRT_BIN%\g++.exe" (
    set "CXX=%UCRT_BIN%\g++.exe"
    set "PATH=%UCRT_BIN%;%PATH%"
) else (
    where g++ >nul 2>nul
    if errorlevel 1 (
        echo ERROR: MSYS2 UCRT64 GCC not found.
        exit /b 1
    )
    set "CXX=g++"
)

if not exist build mkdir build
"%CXX%" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc src\main.cpp -o build\rpg_3d.exe -lraylib -lopengl32 -lgdi32 -lwinmm
if errorlevel 1 (
    echo Compilation failed. Install mingw-w64-ucrt-x86_64-raylib in MSYS2 UCRT64.
    exit /b 1
)
echo Build OK: build\rpg_3d.exe
exit /b 0
