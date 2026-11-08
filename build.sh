#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
# MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-{raylib,sqlite3,openssl}
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc src/main.cpp \
    -o build/rpg_3d.exe -lraylib -lsqlite3 -lcrypto -lopengl32 -lgdi32 -lwinmm
echo "Сборка завершена: ./build/rpg_3d.exe"
