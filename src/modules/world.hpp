#pragma once
/**
 * @file world.hpp
 * @brief Неизменяемая тестовая карта 32x32 без динамических выделений памяти.
 * @details Модуль не знает о рендерере, вводе, коллизиях и RPG Core v3.
 */

#include "api/contracts.hpp"
#include <array>

namespace rpg3d {

/**
 * @brief Хранилище геометрии первого игрового полигона.
 * @details Статические std::array не создают контейнеров в куче; копировать
 *          геометрию при создании Application не требуется.
 * @note Данные сцены пока не меняются. Для динамического мира понадобится
 *       отдельное хранилище с контролируемым временем жизни объектов.
 */
class World final {
public:
    /** @brief Полусторона карты в метрах (координаты от -16 до +16). */
    static constexpr float kHalfExtent = 16.0f;

    /**
     * @brief Получить невладеющее представление геометрии и NPC.
     * @return Наборы объектов и границы карты, без копирования и аллокаций.
     * @note Представление остаётся действительным всё время работы приложения.
     */
    SceneView view() const noexcept {
        return {{kObstacles.data(), kObstacles.size()},
                {kNpcs.data(), kNpcs.size()}, kHalfExtent};
    }

private:
    // Здесь намеренно нет ресурсов в куче и текстовых std::string.
    inline static constexpr std::array<Obstacle, 19> kObstacles{{
        {ObjectKind::Forge,     {  7.5f,  5.2f}, {2.2f, 1.8f}, 3.2f},
        {ObjectKind::Alchemy,   { -7.4f,  4.8f}, {2.2f, 1.8f}, 3.0f},
        {ObjectKind::Carpentry, { -7.4f, -5.9f}, {2.1f, 1.8f}, 2.7f},
        {ObjectKind::House,     {  7.4f, -6.4f}, {2.0f, 1.8f}, 2.8f},
        {ObjectKind::Tree, {-13.0f,  9.0f}, {0.38f, 0.38f}, 2.8f},
        {ObjectKind::Tree, {-11.2f, 12.0f}, {0.38f, 0.38f}, 3.0f},
        {ObjectKind::Tree, {-13.2f, -0.5f}, {0.38f, 0.38f}, 2.8f},
        {ObjectKind::Tree, { 13.0f, 11.0f}, {0.38f, 0.38f}, 2.8f},
        {ObjectKind::Tree, { 12.3f, -0.2f}, {0.38f, 0.38f}, 3.0f},
        {ObjectKind::Tree, { 11.3f,-12.2f}, {0.38f, 0.38f}, 2.8f},
        {ObjectKind::Tree, {-12.7f,-12.1f}, {0.38f, 0.38f}, 2.8f},
        {ObjectKind::Tree, { -8.3f,-12.3f}, {0.38f, 0.38f}, 2.8f},
        {ObjectKind::Ore,      { 2.0f, 12.5f}, {0.85f, 0.75f}, 1.2f},
        {ObjectKind::Ore,      {-1.2f, 13.0f}, {0.70f, 0.70f}, 1.0f},
        {ObjectKind::Rock,     {-4.2f, 10.7f}, {0.70f, 0.65f}, 1.3f},
        {ObjectKind::Rock,     {11.5f, -8.2f}, {0.65f, 0.65f}, 1.2f},
        {ObjectKind::WoodPile,{-9.8f, -9.1f}, {0.85f, 0.65f}, 1.0f},
        {ObjectKind::Fence,    { 3.5f, -9.7f}, {0.25f, 2.0f}, 1.2f},
        {ObjectKind::Fence,    { 0.2f, -9.7f}, {0.25f, 2.0f}, 1.2f}
    }};

    inline static constexpr std::array<Npc, 4> kNpcs{{
        {"Boran", "Blacksmith", "I need a metal blank to forge a blade. In v0.2 we will connect crafting.",
         "", { 4.35f, 4.3f}, 0},
        {"Mira", "Alchemist", "I can refine ore and experiment with elements. The core already knows how.",
         "", {-4.40f, 4.1f}, 1},
        {"Tor", "Carpenter", "Bring me wood and I will shape a handle, when inventory is connected.",
         "", {-4.25f,-5.5f}, 2},
        {"Kela", "Forester", "The trees are only scenery today. Resource gathering is the next step.",
         "", {-9.0f, 9.5f}, 3}
    }};
};

} // namespace rpg3d
