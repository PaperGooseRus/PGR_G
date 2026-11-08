#pragma once
/**
 * @file movement.hpp
 * @brief Чистая кинематика игрока; модуль не знает о карте и столкновениях.
 */

#include "api/contracts.hpp"
#include <algorithm>
#include <cmath>

namespace rpg3d {

/**
 * @brief Вычисляет запрошенное горизонтальное смещение и обновляет прыжок.
 * @note Применение смещения через Collision::slide выполняет Application,
 *       а не Movement. Межмодульные вызовы здесь отсутствуют.
 */
class Movement final {
public:
    /** @brief Максимальный модельный интервал одного кадра в секундах. */
    static constexpr float kMaxDelta = 0.05f;
    /** @brief Обычная скорость перемещения (м/с). */
    static constexpr float kWalkSpeed = 3.8f;
    /** @brief Скорость бега (м/с). */
    static constexpr float kSprintSpeed = 6.0f;

    /**
     * @brief Один шаг кинематики без обработки столкновений.
     * @param player Состояние персонажа; изменяется только высота прыжка.
     * @param input Нормализованный ввод [-1, 1] по каждой оси.
     * @param deltaSeconds Интервал кадра; ограничивается диапазоном [0,0.05].
     * @return Запрошенное смещение XZ, передаваемое системе столкновений.
     * @note Не создаёт объектов в куче. Диагональное ускорение подавляется.
     */
    static Vec2 step(Player& player, MovementInput input, float deltaSeconds) noexcept {
        const float dt = std::clamp(deltaSeconds, 0.0f, kMaxDelta);
        const float lengthSquared = input.forward * input.forward +
                                    input.right * input.right;
        if (lengthSquared > 1.0f) {
            const float inverse = 1.0f / std::sqrt(lengthSquared);
            input.forward *= inverse;
            input.right *= inverse;
        }

        const float speedDt = (input.sprint ? kSprintSpeed : kWalkSpeed) * dt;
        const float sine = std::sin(player.yaw);
        const float cosine = std::cos(player.yaw);
        const Vec2 delta{
            (sine * input.forward + cosine * input.right) * speedDt,
            (cosine * input.forward - sine * input.right) * speedDt,
        };

        if (input.jump && player.jumpHeight == 0.0f) player.verticalSpeed = 5.0f;
        if (player.jumpHeight > 0.0f || player.verticalSpeed > 0.0f) {
            player.verticalSpeed -= 13.0f * dt;
            player.jumpHeight += player.verticalSpeed * dt;
            if (player.jumpHeight <= 0.0f) {
                player.jumpHeight = 0.0f;
                player.verticalSpeed = 0.0f;
            }
        }
        return delta;
    }
};

} // namespace rpg3d
