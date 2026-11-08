#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
# Запускать из MSYS2 UCRT64: без Blender, CMake и внешних ассетов.
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc src/main.cpp -o build/rpg_3d.exe \
  -lraylib -lopengl32 -lgdi32 -lwinmm
echo "Built build/rpg_3d.exe. Run: ./build/rpg_3d.exe"
