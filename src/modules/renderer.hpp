#pragma once
/**
 * @file renderer.hpp
 * @brief Программная геометрия из кубов и простой HUD без динамических строк.
 * @details Модуль является «потребителем» общих данных: не вызывает логику
 *          игры, не владеет сценой, не изменяет игрока или NPC.
 */

#include "api/contracts.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>

namespace rpg3d {

/** @brief Отрисовка прототипа; все методы статические и не хранят состояние. */
class Renderer final {
public:
    /**
     * @brief Нарисовать землю, объекты, NPC и игрока внутри BeginMode3D.
     * @param scene Снимок данных мира.
     * @param player Положение и направление игрока.
     * @pre Активен режим 3D raylib.
     * @note Нет выделений памяти и внешних моделей, только геопримитивы.
     */
    static void scene(SceneView scene, const Player& player) noexcept {
        const float mapSize = scene.halfExtent * 2.0f;
        DrawCube({0.0f, -0.46f, 0.0f}, mapSize, 0.9f, mapSize,
                 Color{106, 143, 100, 255});
        DrawCubeWires({0.0f, -0.46f, 0.0f}, mapSize, 0.9f, mapSize,
                      Color{69, 99, 78, 255});

        // Две непрерывные дороги вместо 46 одинаковых кубов в каждом кадре.
        DrawCube({0.0f, 0.001f, 0.0f}, 23.0f, 0.018f, 2.0f,
                 Color{168, 152, 121, 255});
        DrawCube({0.0f, 0.002f, 0.0f}, 2.0f, 0.018f, 23.0f,
                 Color{168, 152, 121, 255});
        DrawGrid(static_cast<int>(mapSize), 1.0f);

        for (const Obstacle& obstacle : scene.obstacles) draw_obstacle(obstacle);
        for (const Npc& npc : scene.npcs) {
            draw_humanoid(npc.position, 0.0f, npc_color(npc.palette), 0.0f, false);
        }
        draw_humanoid(player.position, player.jumpHeight,
                      Color{69, 133, 201, 255}, player.yaw, true);
    }

    /**
     * @brief Нарисовать ярлыки NPC рядом с героем (2D-проход).
     * @param scene Список NPC; не копируется.
     * @param camera Камера текущего кадра.
     * @param playerPosition Координаты игрока.
     * @pre EndMode3D уже был вызван, но BeginDrawing ещё активен.
     */
    static void labels(SceneView scene, Camera3D camera,
                       Vec2 playerPosition) noexcept {
        for (const Npc& npc : scene.npcs) {
            if (distance_squared(npc.position, playerPosition) > 100.0f) continue;
            const Vector2 screen = GetWorldToScreen(at(npc.position, 2.25f), camera);
            const int width = MeasureText(npc.profession, 18);
            DrawRectangle(static_cast<int>(screen.x - width / 2 - 6),
                          static_cast<int>(screen.y - 4), width + 12, 24,
                          Color{20, 29, 39, 210});
            DrawText(npc.profession, static_cast<int>(screen.x - width / 2),
                     static_cast<int>(screen.y), 18, RAYWHITE);
        }
    }

    /**
     * @brief Нарисовать HUD, диалог и справку в 2D-режиме.
     * @param scene Набор NPC для раскрытия индекса диалога и цели.
     * @param player Состояние героя для отображения координат.
     * @param hud Состояние UI, полученное от Application.
     * @pre BeginDrawing активен, BeginMode3D завершён.
     * @warning activeDialogue и nearby.index должны соответствовать scene;
     *          Application дополнительно проверяет их перед передачей.
     */
    static void hud(SceneView scene, const Player& player,
                    const HudState& hud) noexcept {
        const int width = GetScreenWidth();
        const int height = GetScreenHeight();
        DrawRectangle(14, 14, 375, 76, Color{22, 33, 43, 210});
        DrawText("RPG 3D  |  modular v0.1", 26, 24, 22, RAYWHITE);
        DrawText(TextFormat("Position: %.1f, %.1f   NPC: %d", player.position.x,
                            player.position.z, static_cast<int>(scene.npcs.count)),
                 26, 56, 18, Color{184, 212, 220, 255});
        DrawText("WASD move  |  Mouse look  |  E interact  |  F1 help",
                 18, height - 34, 18, RAYWHITE);
        DrawText("TAB cursor  |  Shift sprint  |  Space jump  |  ESC quit",
                 18, height - 57, 18, RAYWHITE);

        if (hud.nearby.type != Interaction::Type::None &&
            hud.activeDialogue == -1) {
            const char* title = "Resource deposit";
            if (hud.nearby.type == Interaction::Type::Npc &&
                hud.nearby.index < scene.npcs.count) {
                title = scene.npcs[hud.nearby.index].profession;
            }
            const int textWidth = MeasureText(title, 21);
            DrawRectangle(width / 2 - textWidth / 2 - 90, height - 130,
                          textWidth + 180, 42, Color{20, 29, 39, 225});
            DrawText(TextFormat("[E] %s", title),
                     width / 2 - textWidth / 2 - 32, height - 119, 21, RAYWHITE);
        }

        if (hud.activeDialogue >= 0 &&
            static_cast<std::size_t>(hud.activeDialogue) < scene.npcs.count) {
            const Npc& npc = scene.npcs[static_cast<std::size_t>(hud.activeDialogue)];
            DrawRectangle(width / 2 - 380, height - 225, 760, 126,
                          Color{15, 26, 36, 240});
            DrawRectangleLines(width / 2 - 380, height - 225, 760, 126,
                               Color{116, 158, 164, 255});
            DrawText(TextFormat("%s - %s", npc.name, npc.profession),
                     width / 2 - 360, height - 207, 23,
                     Color{243, 215, 147, 255});
            DrawText(npc.dialogueLine1, width / 2 - 360, height - 168, 18, RAYWHITE);
            DrawText(npc.dialogueLine2, width / 2 - 360, height - 143, 18, RAYWHITE);
            DrawText("E - close", width / 2 + 260, height - 123, 15, LIGHTGRAY);
        }
        if (hud.showHelp) {
            DrawRectangle(width / 2 - 315, height / 2 - 140, 630, 250,
                          Color{17, 25, 35, 245});
            DrawRectangleLines(width / 2 - 315, height / 2 - 140, 630, 250,
                               Color{123, 164, 176, 255});
            DrawText("FIRST PLAYABLE SCENE", width / 2 - 287, height / 2 - 114,
                     27, RAYWHITE);
            DrawText("W A S D      Walk relative to the camera", width / 2 - 283,
                     height / 2 - 63, 20, RAYWHITE);
            DrawText("Mouse        Rotate camera / player", width / 2 - 283,
                     height / 2 - 32, 20, RAYWHITE);
            DrawText("E            Talk to nearby NPC", width / 2 - 283,
                     height / 2 - 1, 20, RAYWHITE);
            DrawText("Tab / F1     Release cursor / hide help", width / 2 - 283,
                     height / 2 + 30, 20, RAYWHITE);
            DrawText("Alchemy, inventory and crafting arrive in v0.2+.",
                     width / 2 - 283, height / 2 + 72, 18,
                     Color{181, 209, 218, 255});
        }
        if (hud.cursorReleased) {
            DrawText("CURSOR FREE: press TAB to return to the game",
                     20, 105, 18, YELLOW);
        }
        if (hud.resourceNotice) {
            DrawRectangle(20, 134, 525, 47, Color{19, 31, 42, 228});
            DrawText("Resource gathering will be connected in v0.2",
                     32, 148, 19, RAYWHITE);
        }
        DrawFPS(width - 94, 16);
    }

private:
    /** @brief Перевести плоскую позицию в 3D-координаты raylib. */
    static Vector3 at(Vec2 p, float y) noexcept { return {p.x, y, p.z}; }

    /** @brief Затемнить цвет, не меняя альфа-канал. */
    static Color shade(Color source, float factor) noexcept {
        const auto channel = [factor](unsigned char c) -> unsigned char {
            return static_cast<unsigned char>(std::clamp(c * factor, 0.0f, 255.0f));
        };
        return {channel(source.r), channel(source.g), channel(source.b), source.a};
    }

    /** @brief Преобразовать тип объекта в цвет для кубической визуализации. */
    static Color color(ObjectKind kind) noexcept {
        switch (kind) {
            case ObjectKind::Forge:     return {146, 88, 72, 255};
            case ObjectKind::Alchemy:   return {87, 99, 146, 255};
            case ObjectKind::Carpentry: return {177, 132, 74, 255};
            case ObjectKind::House:     return {178, 153, 111, 255};
            case ObjectKind::Tree:      return {110, 76, 49, 255};
            case ObjectKind::Rock:      return {117, 125, 126, 255};
            case ObjectKind::Fence:     return {121, 85, 58, 255};
            case ObjectKind::Ore:       return {91, 108, 139, 255};
            case ObjectKind::WoodPile:  return {140, 98, 59, 255};
        }
        return GRAY;
    }

    /** @brief Нарисовать один элемент статической геометрии. */
    static void draw_obstacle(const Obstacle& o) noexcept {
        const Color base = color(o.kind);
        const Vector3 center = at(o.position, o.height / 2.0f);
        const float width = o.halfSize.x * 2.0f;
        const float depth = o.halfSize.z * 2.0f;
        if (o.kind == ObjectKind::Tree) {
            DrawCube(center, width, o.height, depth, base);
            DrawCube(at(o.position, o.height + 0.4f), 2.0f, 1.5f, 2.0f,
                     Color{54, 116, 71, 255});
            DrawCube(at(o.position, o.height + 1.15f), 1.4f, 0.7f, 1.4f,
                     Color{72, 145, 78, 255});
            return;
        }
        DrawCube(center, width, o.height, depth, base);
        DrawCubeWires(center, width, o.height, depth, shade(base, 0.55f));

        if (o.kind == ObjectKind::Forge || o.kind == ObjectKind::Alchemy ||
            o.kind == ObjectKind::Carpentry || o.kind == ObjectKind::House) {
            DrawCube(at(o.position, o.height + 0.23f), width + 0.45f, 0.45f,
                     depth + 0.45f, shade(base, 0.65f));
            DrawCube({o.position.x - width * 0.3f, 1.6f,
                      o.position.z - o.halfSize.z - 0.03f},
                     0.66f, 0.65f, 0.09f, Color{173, 222, 225, 255});
            DrawCube({o.position.x + width * 0.25f, 0.8f,
                      o.position.z - o.halfSize.z - 0.04f},
                     0.9f, 1.6f, 0.1f, shade(base, 0.45f));
        }
        if (o.kind == ObjectKind::Forge) {
            DrawCube({o.position.x + 1.0f, o.height + 1.05f, o.position.z},
                     0.7f, 1.8f, 0.7f, Color{93, 86, 79, 255});
        }
        if (o.kind == ObjectKind::Ore) {
            DrawCube(at(o.position, o.height + 0.04f),
                     width * 0.55f, 0.08f, depth * 0.55f,
                     Color{98, 190, 209, 255});
        }
        if (o.kind == ObjectKind::WoodPile) {
            for (int i = -1; i <= 1; ++i) {
                DrawCube({o.position.x + i * 0.42f, o.height + 0.16f,
                          o.position.z}, 0.36f, 0.31f, 1.1f,
                         Color{166, 112, 65, 255});
            }
        }
    }

    /** @brief Получить цвет одежды NPC без разыменования динамической таблицы. */
    static Color npc_color(std::uint8_t index) noexcept {
        static constexpr Color palette[4] = {
            {169, 92, 71, 255}, {109, 105, 180, 255},
            {200, 139, 69, 255}, {91, 156, 94, 255}
        };
        return palette[index % 4];
    }

    /** @brief Нарисовать персонажа из шести кубов и маркера направления. */
    static void draw_humanoid(Vec2 position, float baseY, Color clothes,
                              float yaw, bool player) noexcept {
        const Color skin{231, 188, 151, 255};
        DrawCube({position.x, baseY + 0.94f, position.z}, 0.74f, 0.9f, 0.43f,
                 clothes);
        DrawCube({position.x, baseY + 1.67f, position.z}, 0.57f, 0.55f, 0.52f,
                 skin);
        DrawCube({position.x - 0.48f, baseY + 0.92f, position.z},
                 0.23f, 0.79f, 0.36f, clothes);
        DrawCube({position.x + 0.48f, baseY + 0.92f, position.z},
                 0.23f, 0.79f, 0.36f, clothes);
        DrawCube({position.x - 0.19f, baseY + 0.27f, position.z},
                 0.28f, 0.55f, 0.35f, shade(clothes, 0.65f));
        DrawCube({position.x + 0.19f, baseY + 0.27f, position.z},
                 0.28f, 0.55f, 0.35f, shade(clothes, 0.65f));
        if (player) {
            DrawCube({position.x + std::sin(yaw) * 0.29f, baseY + 1.68f,
                      position.z + std::cos(yaw) * 0.29f},
                     0.18f, 0.12f, 0.16f, Color{29, 50, 71, 255});
        }
    }
};

} // namespace rpg3d
