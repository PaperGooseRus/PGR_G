#pragma once
/**
 * @file interaction.hpp
 * @brief Поиск ближайшего NPC или ресурса без выделений и извлечения корня.
 */

#include "api/contracts.hpp"

namespace rpg3d {

/** @brief Независимый поиск доступного для взаимодействия объекта. */
class InteractionSystem final {
public:
    /**
     * @brief Найти ближайшую цель в заданном радиусе.
     * @param scene Геометрия и NPC (предоставляет Application).
     * @param position Положение игрока на XZ.
     * @param maxDistance Максимальная дальность взаимодействия в метрах.
     * @return Цель с индексом в исходном View или тип None.
     * @note В горячем цикле сравниваются только квадраты расстояний.
     *       При равной дистанции приоритет у ранее найденного NPC.
     */
    static Interaction closest(SceneView scene, Vec2 position,
                               float maxDistance = 2.5f) noexcept {
        Interaction best{};
        float best2 = maxDistance * maxDistance;
        for (std::size_t i = 0; i < scene.npcs.count; ++i) {
            const float d2 = distance_squared(position, scene.npcs[i].position);
            if (d2 < best2) {
                best2 = d2;
                best = {Interaction::Type::Npc, i, d2};
            }
        }
        for (std::size_t i = 0; i < scene.obstacles.count; ++i) {
            const Obstacle& obstacle = scene.obstacles[i];
            if (obstacle.kind != ObjectKind::Ore &&
                obstacle.kind != ObjectKind::WoodPile &&
                obstacle.kind != ObjectKind::Tree &&
                obstacle.kind != ObjectKind::Rock) continue;
            const float d2 = distance_squared(position, obstacle.position);
            if (d2 < best2) {
                best2 = d2;
                best = {Interaction::Type::Resource, i, d2};
            }
        }
        return best;
    }
};

} // namespace rpg3d
