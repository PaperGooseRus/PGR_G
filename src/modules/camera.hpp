#pragma once
/**
 * @file camera.hpp
 * @brief Позиционирование 3D-камеры и защита от пересечения с геометрией.
 * @details Зависимости: только общий контракт и raylib; без ссылок на World,
 *          Movement или Renderer.
 */

#include "api/contracts.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>

namespace rpg3d {

/** @brief Самостоятельная система камеры от третьего лица. */
class CameraSystem final {
public:
    /**
     * @brief Сформировать камеру и ограничить расстояние до ближайшей стены.
     * @param scene Невладеющий снимок сцены.
     * @param player Текущее положение и ориентация героя.
     * @param pitch Угол подъёма в радианах.
     * @param distance Желаемая дистанция до героя, метры; строго > 0.
     * @return Готовая Camera3D для BeginMode3D.
     * @note При текущих 19 препятствиях линейный поиск выгоднее индекса:
     *       нет вспомогательного дерева и дополнительных выделений памяти.
     */
    static Camera3D make(SceneView scene, const Player& player,
                         float pitch, float distance) noexcept {
        Camera3D camera{};
        camera.target = {player.position.x, player.jumpHeight + 1.30f,
                         player.position.z};
        camera.up = {0.0f, 1.0f, 0.0f};
        camera.fovy = 62.0f;
        camera.projection = CAMERA_PERSPECTIVE;

        const float horizontal = distance * std::cos(pitch);
        const Vector3 desired{
            camera.target.x - std::sin(player.yaw) * horizontal,
            camera.target.y + distance * std::sin(pitch),
            camera.target.z - std::cos(player.yaw) * horizontal
        };
        const Vector3 offset{desired.x - camera.target.x,
                             desired.y - camera.target.y,
                             desired.z - camera.target.z};
        const float length2 = offset.x * offset.x + offset.y * offset.y +
                              offset.z * offset.z;
        if (length2 <= 0.000001f) {
            camera.position = desired;
            return camera;
        }

        const float length = std::sqrt(length2);
        const float inverse = 1.0f / length;
        const Ray ray{camera.target, {offset.x * inverse, offset.y * inverse,
                                      offset.z * inverse}};
        float nearest = length;
        for (const Obstacle& obstacle : scene.obstacles) {
            const float top = obstacle.height +
                (obstacle.kind == ObjectKind::Tree ? 1.7f : 0.5f);
            const BoundingBox box{
                {obstacle.position.x - obstacle.halfSize.x, 0.0f,
                 obstacle.position.z - obstacle.halfSize.z},
                {obstacle.position.x + obstacle.halfSize.x, top,
                 obstacle.position.z + obstacle.halfSize.z}
            };
            const RayCollision hit = GetRayCollisionBox(ray, box);
            if (hit.hit && hit.distance < nearest) nearest = hit.distance;
        }
        const float safeDistance = std::max(0.5f, nearest - 0.25f);
        camera.position = {
            camera.target.x + ray.direction.x * safeDistance,
            camera.target.y + ray.direction.y * safeDistance,
            camera.target.z + ray.direction.z * safeDistance
        };
        return camera;
    }
};

} // namespace rpg3d
