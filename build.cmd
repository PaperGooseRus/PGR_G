@echo off
setlocal EnableExtensions
cd /d "%~dp0"
REM Компиляция MSYS2 UCRT64; остальные C++-пакетные менеджеры не нужны.
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "UCRT_BIN=%MSYS2_ROOT%\ucrt64\bin"
if exist "%UCRT_BIN%\g++.exe" (
    set "CXX=%UCRT_BIN%\g++.exe"
    set "PATH=%UCRT_BIN%;%PATH%"
) else (
    where g++ >nul 2>nul
    if errorlevel 1 (
        echo MSYS2 GCC не найден.
        exit /b 1
    )
    set "CXX=g++"
)
if not exist build mkdir build
"%CXX%" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc src\main.cpp -o build\rpg_3d.exe -lraylib -lsqlite3 -lcrypto -lopengl32 -lgdi32 -lwinmm
if errorlevel 1 (
    echo Ошибка сборки. Установи raylib, sqlite3 и openssl для MSYS2 UCRT64.
    exit /b 1
)
echo RPG 3D собрана: build\rpg_3d.exe
exit /b 0
