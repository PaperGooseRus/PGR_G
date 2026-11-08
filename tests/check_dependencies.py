#!/usr/bin/env python3
"""Проверка: модули не включают друг друга и ядро v3 напрямую."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
modules = sorted((root / "src" / "modules").glob("*.hpp"))
assert len(modules) == 6, "Ожидались 6 независимых модулей"
for module in modules:
    body = module.read_text(encoding="utf-8")
    local = re.findall(r'^\s*#\s*include\s*"([^"]+)"', body, flags=re.M)
    assert set(local) <= {"api/contracts.hpp", "raylib.h"}, (module, local)
    assert "core_v3" not in body, module
    assert "/**" in body and "@file" in body, module

contracts = (root / "src" / "api" / "contracts.hpp").read_text(encoding="utf-8")
assert "raylib.h" not in contracts and "core_v3" not in contracts
main = (root / "src" / "main.cpp").read_text(encoding="utf-8")
assert "class Application final" in main
assert all(f'"modules/{module.name}"' in main for module in modules)
print("Dependency rules: OK (6 independent modules, 1 orchestrator)")
