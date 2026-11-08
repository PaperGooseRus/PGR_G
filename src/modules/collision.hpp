#pragma once
/**
 * @file collision.hpp
 * @brief Проверка пересечений и скольжения, независимо от мира и движения.
 */

#include "api/contracts.hpp"
#include <algorithm>

namespace rpg3d {

/**
 * @brief Алгоритмы столкновений на плоскости XZ.
 * @details Не изменяют игровые данные; время O(число препятствий + NPC).
 */
class Collision final {
public:
    /** @brief Радиус коллайдера игрока по умолчанию, метры. */
    static constexpr float kPlayerRadius = 0.32f;

    /**
     * @brief Проверить пересечение круга и прямоугольника AABB.
     * @param center Центр окружности.
     * @param radius Её радиус, неотрицательное значение.
     * @param rectCenter Центр прямоугольника.
     * @param halfSize Полуразмеры прямоугольника.
     * @return true при пересечении; касание границ не считается проникновением.
     */
    static bool overlaps(Vec2 center, float radius, Vec2 rectCenter,
                         Vec2 halfSize) noexcept {
        const float x = std::clamp(center.x, rectCenter.x - halfSize.x,
                                  rectCenter.x + halfSize.x);
        const float z = std::clamp(center.z, rectCenter.z - halfSize.z,
                                  rectCenter.z + halfSize.z);
        return distance_squared(center, {x, z}) < radius * radius;
    }

    /**
     * @brief Проверить границы карты, препятствия и NPC.
     * @param scene Снимок карты (не владеет данными).
     * @param point Проверяемая позиция игрока.
     * @param radius Радиус кругового коллайдера.
     * @return true, если позиция занята или выходит за границы.
     */
    static bool blocked(SceneView scene, Vec2 point,
                        float radius = kPlayerRadius) noexcept {
        if (point.x - radius < -scene.halfExtent ||
            point.x + radius > scene.halfExtent ||
            point.z - radius < -scene.halfExtent ||
            point.z + radius > scene.halfExtent) return true;

        for (const Obstacle& obstacle : scene.obstacles) {
            if (overlaps(point, radius, obstacle.position, obstacle.halfSize)) return true;
        }
        constexpr float npcRadius = 0.38f;
        const float limit2 = (radius + npcRadius) * (radius + npcRadius);
        for (const Npc& npc : scene.npcs) {
            if (distance_squared(point, npc.position) < limit2) return true;
        }
        return false;
    }

    /**
     * @brief Переместить окружность, разрешив X и Z независимо (скольжение).
     * @param scene Данные столкновений.
     * @param position Исходная позиция.
     * @param displacement Желаемое смещение за кадр, в метрах.
     * @param radius Радиус тела.
     * @return Разрешённая позиция, без изменения состояния сцены.
     * @warning Метод дискретный: при слишком большом смещении возможно
     *          прохождение через препятствие. При обычном dt<=0.05 и текущих
     *          скоростях максимальный шаг меньше толщины барьеров.
     */
    static Vec2 slide(SceneView scene, Vec2 position, Vec2 displacement,
                      float radius = kPlayerRadius) noexcept {
        if (!blocked(scene, {position.x + displacement.x, position.z}, radius)) {
            position.x += displacement.x;
        }
        if (!blocked(scene, {position.x, position.z + displacement.z}, radius)) {
            position.z += displacement.z;
        }
        return position;
    }
};

} // namespace rpg3d
