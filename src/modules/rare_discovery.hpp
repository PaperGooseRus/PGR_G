#pragma once
/** @file rare_discovery.hpp
 *  @brief Применение тайных правил и честного криптографического случайного выбора.
 *  @warning Это НЕ VRF: оператор локального мира может изменить файл БД.
 */
#include "api/economy_contracts.hpp"
#include <openssl/rand.h>
#include <cstdint>
#include <stdexcept>

namespace rpg3d {
/** @brief Статeless-проверка, без хранения игрока и обращения к SQLite. */
class RareDiscovery final {
public:
    static constexpr std::uint32_t kDenominator = 1000000U;

    /** @brief Сверить условия опыта с тайным правилом мира. */
    static bool eligible(const RareContext& c, const RareRule& r) noexcept {
        return c.atLaboratory && c.jumps >= r.requiredJumps &&
               c.rituals >= r.requiredRituals && c.direction == r.requiredDirection;
    }

    /**
     * @brief Сгенерировать равномерное случайное значение [0,999999].
     * @return Значение в диапазоне без смещения по модулю.
     * @throws std::runtime_error Если защищённый ГСЧ недоступен.
     */
    static std::uint32_t secure_roll() {
        const std::uint64_t total = (std::uint64_t{1} << 32);
        const std::uint64_t limit = total - total % kDenominator;
        std::uint32_t number = 0;
        do {
            if (RAND_bytes(reinterpret_cast<unsigned char*>(&number), sizeof(number)) != 1)
                throw std::runtime_error("Не удалось получить криптографическую случайность");
        } while (number >= limit);
        return number % kDenominator;
    }

    /** @brief Только нулевой исход из миллиона означает успех. */
    static bool success(std::uint32_t roll) noexcept { return roll == 0; }
};
} // namespace rpg3d
