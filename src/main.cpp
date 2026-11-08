/**
 * @file main.cpp
 * @brief Единственная точка сборки независимых модулей через Application.
 * @details Ни один модуль из src/modules не включает другие модули.
 *          Application управляет жизненным циклом, подключает ввод,
 *          движение, коллизии, взаимодействие, камеру и рендерер.
 */

#include "modules/world.hpp"
#include "modules/collision.hpp"
#include "modules/movement.hpp"
#include "modules/interaction.hpp"
#include "modules/camera.hpp"
#include "modules/renderer.hpp"
#include "raylib.h"
#include <algorithm>
#include <cstddef>

namespace rpg3d {

/**
 * @brief Корневой координатор клиентского приложения.
 * @details Только здесь допускается последовательное использование разных
 *          модулей. Владеет состоянием игрока и интерфейса, но не реализует
 *          коллизии, правила движения или рисование.
 * @note Объект приложения создаётся один раз; горячий цикл не выделяет память.
 */
class Application final {
public:
    /**
     * @brief Инициализировать raylib, выполнить цикл игры, освободить окно.
     * @return 0 после корректного завершения.
     * @note Должен вызываться из основного потока.
     */
    int run() {
        SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
        InitWindow(1280, 720, "RPG 3D - Cube Village v0.1");
        SetWindowMinSize(960, 540);
        SetTargetFPS(60);
        DisableCursor();

        const SceneView scene = world_.view();
        while (!WindowShouldClose()) {
            const float dt = std::clamp(GetFrameTime(), 0.0f, Movement::kMaxDelta);
            update(scene, dt);
            draw(scene);
        }

        EnableCursor();
        CloseWindow();
        return 0;
    }

private:
    /**
     * @brief Считать ввод и связать результаты модулей в одном кадре.
     * @param scene Статическое представление мира.
     * @param dt Длительность модельного шага, сек.
     */
    void update(SceneView scene, float dt) {
        if (IsKeyPressed(KEY_TAB)) {
            cursorReleased_ = !cursorReleased_;
            if (cursorReleased_) EnableCursor(); else DisableCursor();
        }
        if (IsKeyPressed(KEY_F1)) showHelp_ = !showHelp_;
        resourceNoticeSeconds_ = std::max(0.0f, resourceNoticeSeconds_ - dt);

        const bool controlled = !cursorReleased_ && !showHelp_ && IsWindowFocused();
        if (controlled) {
            const Vector2 mouse = GetMouseDelta();
            player_.yaw += mouse.x * 0.0035f;
            cameraPitch_ = std::clamp(cameraPitch_ + mouse.y * 0.0025f,
                                      0.13f, 1.14f);
            cameraDistance_ = std::clamp(cameraDistance_ - GetMouseWheelMove(),
                                         3.0f, 9.0f);
            const MovementInput input{
                static_cast<float>(IsKeyDown(KEY_W)) -
                    static_cast<float>(IsKeyDown(KEY_S)),
                static_cast<float>(IsKeyDown(KEY_D)) -
                    static_cast<float>(IsKeyDown(KEY_A)),
                IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT),
                IsKeyPressed(KEY_SPACE)
            };
            const Vec2 displacement = Movement::step(player_, input, dt);
            player_.position = Collision::slide(scene, player_.position, displacement);
        }

        nearby_ = InteractionSystem::closest(scene, player_.position);
        if (activeDialogue_ >= 0) {
            const auto index = static_cast<std::size_t>(activeDialogue_);
            if (index >= scene.npcs.count ||
                distance_squared(player_.position, scene.npcs[index].position) > 12.25f) {
                activeDialogue_ = -1;
            }
        }
        if (controlled && IsKeyPressed(KEY_E)) {
            if (activeDialogue_ >= 0) {
                activeDialogue_ = -1;
            } else if (nearby_.type == Interaction::Type::Npc &&
                       nearby_.index < scene.npcs.count) {
                activeDialogue_ = static_cast<int>(nearby_.index);
            } else if (nearby_.type == Interaction::Type::Resource) {
                // В v0.1 ресурс не поступает в инвентарь: ядро пока отдельно.
                resourceNoticeSeconds_ = 3.0f;
            }
        }
    }

    /** @brief Скомпоновать 3D- и 2D-проходы текущего кадра. */
    void draw(SceneView scene) const {
        const Camera3D camera = CameraSystem::make(
            scene, player_, cameraPitch_, cameraDistance_);
        BeginDrawing();
        ClearBackground(Color{138, 190, 215, 255});
        BeginMode3D(camera);
        Renderer::scene(scene, player_);
        EndMode3D();
        Renderer::labels(scene, camera, player_.position);
        const HudState hud{
            nearby_, activeDialogue_, showHelp_, cursorReleased_,
            resourceNoticeSeconds_ > 0.0f
        };
        Renderer::hud(scene, player_, hud);
        EndDrawing();
    }

    World world_{};                         ///< Статическая карта без аллокаций.
    Player player_{};                       ///< Единственное состояние игрока.
    Interaction nearby_{};                 ///< Найденный интерактивный объект.
    float cameraPitch_ = 0.36f;            ///< Вертикальный угол камеры.
    float cameraDistance_ = 5.9f;          ///< Дистанция камеры.
    float resourceNoticeSeconds_ = 0.0f;   ///< Таймер предупреждения.
    int activeDialogue_ = -1;              ///< Диалог открыт для NPC с этим индексом.
    bool cursorReleased_ = false;          ///< Режим курсора.
    bool showHelp_ = false;                ///< Окно справки.
};

} // namespace rpg3d

/** @brief Точка входа игры. Создаёт единственный Application. */
int main() {
    rpg3d::Application app;
    return app.run();
}
