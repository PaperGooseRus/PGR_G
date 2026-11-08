# RPG 3D Client: API и правила использования

> API графического клиента **исходного уровня (C++17)**. Не путать с внешним сетевым API или бинарным ABI. Подробности каждого типа и метода находятся в Doxygen-комментариях `src/api/contracts.hpp` и `src/modules/*.hpp`.

## Публичный API независимых модулей

| Тип | Интерфейс | Назначение, сложность |
|---|---|---|
| `World` | `SceneView view() const noexcept` | Невладеющий вид статической геометрии, O(1), без аллокаций |
| `Collision` | `blocked(scene, point, radius)` | Пересечение карты и NPC, O(N+M), без аллокаций |
| `Collision` | `slide(scene, point, delta, radius)` | Скольжение по осям, до двух проверок blocked |
| `Collision` | `overlaps(center, radius, rectCenter, halfSize)` | Круг и AABB, O(1) |
| `Movement` | `step(player, input, dt)` | Только кинематика и высота прыжка; возвращает `Vec2` |
| `InteractionSystem` | `closest(scene, position, maxDistance)` | Поиск NPC/ресурса по квадрату дистанции, O(N+M) |
| `CameraSystem` | `make(scene, player, pitch, distance)` | Камера с raycast AABB, требует raylib |
| `Renderer` | `scene(scene, player)` | Рисование 3D-примитивов в активном BeginMode3D |
| `Renderer` | `labels(scene, camera, position)` | 2D-подписи NPC |
| `Renderer` | `hud(scene, player, hud)` | 2D-интерфейс и диалоги |
| `Application` | `run()` | Корневая координация модулей, определён только в main.cpp |

### Контракты на данные

* `Vec2` задаёт горизонтальные **X и Z** в метрах; вертикальная ось принадлежит 3D-представлению.
* `SceneView` содержит два невладеющих `View<T>` и границы карты; копирование `SceneView` не копирует массивы.
* `Npc` содержит **статические** строки, все поля доступны только для чтения из сцены. Для локализованных строк нужен отдельный модуль/слой ресурсов.
* `Player` содержит только трансформ и визуальный прыжок. Не путать с персонажем `Actor` из `core_v3`: они ещё не связаны.
* `Interaction::index` обращается к соответствующему набору `SceneView` и актуален в текущем снимке мира. Для `Type::None` индекс недействителен.
* `HudState::activeDialogue` должен быть `-1` или корректным индексом NPC.

### Пример внешнего использования без raylib

```cpp
#include "modules/world.hpp"
#include "modules/movement.hpp"
#include "modules/collision.hpp"
#include "modules/interaction.hpp"

int main() {
    rpg3d::World world;
    rpg3d::Player hero;
    const rpg3d::SceneView scene = world.view();

    const rpg3d::MovementInput input{1.0f, 0.0f, false, false};
    const rpg3d::Vec2 proposed = rpg3d::Movement::step(hero, input, 1.0f / 60.0f);
    hero.position = rpg3d::Collision::slide(scene, hero.position, proposed);

    const auto target = rpg3d::InteractionSystem::closest(scene, hero.position);
    return target.type == rpg3d::Interaction::Type::None ? 0 : 1;
}
```

Компиляция (Linux / MSYS2, без raylib):

```bash
g++ -std=c++17 -Isrc example.cpp -o example
```

### Использование камеры и рендерера

`CameraSystem` и `Renderer` зависят **только** от `raylib.h` и `contracts.hpp`, но не друг от друга. Внешний вызывающий код обязан:

1. Инициализировать окно `InitWindow()` в том же потоке.
2. Каждый кадр получить актуальные `SceneView` и `Player`.
3. Вызвать `CameraSystem::make()`.
4. Обрамить `Renderer::scene` вызовами `BeginDrawing`, `BeginMode3D`, `EndMode3D`, `EndDrawing`.
5. После выхода из 3D вызвать `Renderer::labels` и `Renderer::hud`.
6. В конце вызвать `CloseWindow()`.

В этой версии вызывающий код — **только `Application` из `src/main.cpp`**.

## Принципы расширения

**Единообразие:** методы названы по действию (`step`, `slide`, `closest`, `make`) и принимают вид данных вместо конкретного класса другого модуля. Всё, что отрисовывается, приходит из состояния, не из скрытого глобального синглтона.

**Типовые сценарии быстрые:** один статический мир, без сериализации или файлового I/O в кадре; поиск расстояний без `sqrt`; движение без пересборки сцены; UI без временных строк.

**Чем меньше, тем быстрее (когда это обосновано):** низкая аллокационная нагрузка, компактный набор модулей, отсутствие тяжёлых абстракций. Меньший объём кода сам по себе не гарантирует скорость; изменения оцениваются тестами и измерениями.

## Генерация Doxygen

Установите Doxygen любым доступным способом, затем из корня проекта:

```bash
doxygen Doxyfile
```

Откройте `build/doxygen/html/index.html`. Если используется CMake и найден Doxygen, доступна команда `cmake --build build --target docs`.
