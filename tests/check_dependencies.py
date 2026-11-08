#!/usr/bin/env python3
"""Архитектурная проверка: связь модулей только в Application из main.cpp."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
modules = sorted((root / "src" / "modules").glob("*.hpp"))
expected = {"camera", "collision", "interaction", "movement", "renderer", "world",
            "ledger_store", "trade_rules", "rare_discovery"}
assert {m.stem for m in modules} == expected, "Неверный набор модулей"
for module in modules:
    text = module.read_text(encoding="utf-8")
    includes = re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, flags=re.M)
    allowed = {"api/contracts.hpp", "api/economy_contracts.hpp", "raylib.h"}
    assert set(includes) <= allowed, (module, includes)
    assert 'core_v3' not in text, module
    assert '/**' in text and '@file' in text, module
for file in (root / "src" / "api").glob("*.hpp"):
    text = file.read_text(encoding="utf-8")
    assert 'raylib.h' not in text and 'sqlite3.h' not in text and 'core_v3' not in text
main = (root / "src" / "main.cpp").read_text(encoding="utf-8")
assert "class Application final" in main
for module in modules:
    assert f'"modules/{module.name}"' in main, module
print(f"Модульность: OK ({len(modules)} независимых модулей)")
