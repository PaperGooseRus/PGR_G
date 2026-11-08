# RPG 3D Client v0.1-refactor (Cube Village)

Переработанная версия первой кубической 3D-сцены RPG для **одного разработчика**: C++17, VS Code, GCC, MSYS2 UCRT64, raylib. Без Blender, игровых ассетов, сервера, базы данных и дополнительных игровых движков.

**Сделано:** разделены шесть независимых модулей 3D-клиента. Они используют только `src/api/contracts.hpp`, не включают друг друга и вызываются **исключительно классом `Application` в `src/main.cpp`**. Везде, где это важно для внешнего и внутреннего взаимодействия, добавлены русскоязычные Doxygen-комментарии и отдельные руководства по API.

**Сохранено:** 3D-персонаж, камера от третьего лица, кубическая карта 32×32, коллизии, визуальный прыжок, 4 неподвижных NPC, диалоги и подсказки. **Не добавлялось:** настоящий графический инвентарь, добыча, крафт и 3D-интеграция алхимического ядра. Это по-прежнему задача следующего этапа.

## Установка на Windows (MSYS2 UCRT64)

В терминале MSYS2 UCRT64:

```bash
pacman -S mingw-w64-ucrt-x86_64-raylib
```

Собрать и запустить:

```bash
./build.sh
./build/rpg_3d.exe
```

Или в обычном Windows CMD из каталога проекта вызвать `build.cmd`, затем `run.cmd` (MSYS2 установлен в `C:\msys64` или задан `MSYS2_ROOT`). Сцене не нужны текстуры и файлы моделей.

Управление: `WASD` ходьба, мышь поворот, `Shift` бег, `Space` прыжок, `E` диалог/осмотр ресурса, `F1` справка, `Tab` курсор, колесо мыши изменение дистанции, `Esc` выход.

## Структура

```text
rpg_3d_refactored_v0_1/
├── src/
│   ├── api/
│   │   └── contracts.hpp       # Минимальные структуры данных, view и типы
│   ├── modules/
│   │   ├── world.hpp           # Только статический мир
│   │   ├── collision.hpp       # Только геометрические столкновения
│   │   ├── movement.hpp        # Только кинематика
│   │   ├── interaction.hpp     # Только поиск NPC/ресурсов
│   │   ├── camera.hpp          # Только камера (raylib)
│   │   └── renderer.hpp        # Только отрисовка (raylib)
│   └── main.cpp                # class Application: вся композиция модулей
├── core_v3/                   # Сохранённое legacy RPG Core v3
├── tests/
│   ├── test_modules.cpp
│   ├── check_dependencies.py
│   └── raylib_stub/           # Только для headless-проверки клиента
├── docs/
│   ├── API.md
│   ├── ARCHITECTURE.md
│   ├── OPTIMIZATION.md
│   └── NEXT_STEPS.md
├── Doxyfile
├── CMakeLists.txt
├── build.cmd
├── build.sh
└── run.cmd
```

## Тесты без raylib (обычный GCC и CMake)

```bash
cmake -S . -B build-test -DRPG_BUILD_CLIENT=OFF -DRPG_BUILD_STUB_SMOKE=ON -DBUILD_TESTING=ON
cmake --build build-test -j 2
ctest --test-dir build-test --output-on-failure
```

Эти команды собирают проверку шести модулей, независимости заголовков, прежние тесты RPG Core и **один headless-кадр** с заглушкой raylib. Реальный рендер и FPS можно проверить только в MSYS2 с raylib.

Если нужен только исходный клиент без заглушки (в среде с raylib):

```bash
cmake -S . -B build-client -DRPG_BUILD_CLIENT=ON
cmake --build build-client
```

## Документация API

- [docs/API.md](docs/API.md) — публичные контракты, примеры включения и вызовов, инварианты.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — строгая схема зависимостей и порядок кадра.
- [docs/OPTIMIZATION.md](docs/OPTIMIZATION.md) — конкретные изменения производительности и ограничения.
- [docs/NEXT_STEPS.md](docs/NEXT_STEPS.md) — 5 небольших проверяемых этапов развития.
- `src/api/contracts.hpp`, `src/modules/*.hpp`, `src/main.cpp` — **Doxygen-комментарии** у публичных типов, полей и операций.

Для HTML-документации (Doxygen устанавливается отдельно):

```bash
doxygen Doxyfile
```

Результат: `build/doxygen/html/index.html`. Документация не требуется для сборки программы.

## Архитектурные ограничения

`core_v3/` оставлен нетронутым и компилируется самостоятельным `rpg_core_cli`. Его **внутренние** зависимости пока не рефакторились: изменения в экономике, сохранениях и алхимии не входили в безопасный объём этого шага. Новое правило «модули не включают друг друга» гарантируется для `src/modules/` и проверяется автоматическим тестом.

Общие типы из `contracts.hpp` — это C++-контракт уровня исходников, не стабильный сетевой протокол или бинарный ABI. Для подключения настоящих RPG-предметов нужен отдельный адаптер, который будет вызываться `Application`, а не рендерером.
