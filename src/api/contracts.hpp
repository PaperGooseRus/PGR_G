#pragma once
/**
 * @file contracts.hpp
 * @brief Общие контракты клиента; никакой игровой логики и зависимости от raylib.
 * @details Это единственный общий заголовок модулей. Данные передаются как
 *          простые структуры и невладеющие представления, без выделений памяти.
 */

#include <cstddef>
#include <cstdint>

namespace rpg3d {

/** @defgroup contracts Контракты игровых модулей
 *  @brief Общие неизменяемые виды данных и небольшие структуры состояния.
 *  @{ */

/** @brief Двумерная точка или вектор на горизонтальной плоскости XZ (метры). */
struct Vec2 {
    float x = 0.0f; ///< Мировая координата X.
    float z = 0.0f; ///< Мировая координата Z.
};

/**
 * @brief Невладеющий непрерывный диапазон объектов, аналог std::span для C++17.
 * @tparam T Тип элемента.
 * @warning Память должна существовать дольше, чем это представление.
 *          Представление не выполняет проверку границ при индексировании.
 */
template <typename T>
struct View {
    const T* data = nullptr; ///< Первый элемент; nullptr для пустого диапазона.
    std::size_t count = 0;   ///< Количество доступных элементов.

    /** @brief Получить объект по проверенному вызывающим индексу. */
    constexpr const T& operator[](std::size_t index) const noexcept { return data[index]; }
    /** @brief Начало диапазона для range-for. */
    constexpr const T* begin() const noexcept { return data; }
    /** @brief Конец диапазона для range-for. */
    constexpr const T* end() const noexcept { return data ? data + count : nullptr; }
    /** @brief Проверить, что диапазон пуст. */
    constexpr bool empty() const noexcept { return count == 0; }
};

/** @brief Классификация статических препятствий и видимых ресурсов. */
enum class ObjectKind : std::uint8_t {
    Forge,       ///< Кузница.
    Alchemy,     ///< Алхимическая лаборатория.
    Carpentry,   ///< Плотницкая мастерская.
    House,       ///< Дом.
    Tree,        ///< Дерево.
    Rock,        ///< Камень.
    Fence,       ///< Забор.
    Ore,         ///< Рудное месторождение (декорация v0.1).
    WoodPile     ///< Древесина (декорация v0.1).
};

/** @brief Неподвижная геометрия с прямоугольным коллайдером XZ. */
struct Obstacle {
    ObjectKind kind;  ///< Вид объекта (визуализация и разрешённые действия).
    Vec2 position;    ///< Центр коллайдера.
    Vec2 halfSize;    ///< Полуразмеры коллайдера XZ.
    float height;     ///< Высота видимого объекта.
};

/**
 * @brief Текстовые и геометрические данные NPC без собственного поведения.
 * @details Реплики разделены заранее, чтобы HUD не создавал std::string в кадре.
 */
struct Npc {
    const char* name;          ///< Имя в ASCII/UTF-8; статический срок жизни.
    const char* profession;    ///< Отображаемая профессия.
    const char* dialogueLine1; ///< Первая строка краткой реплики.
    const char* dialogueLine2; ///< Вторая строка, может быть пустой.
    Vec2 position;            ///< Положение NPC на карте.
    std::uint8_t palette;     ///< Индекс цвета модели.
};

/**
 * @brief Снимок данных сцены для всех независимых модулей.
 * @note Представления не владеют данными и остаются действительными,
 *       пока существует поставивший их объект World.
 */
struct SceneView {
    View<Obstacle> obstacles; ///< Все статические препятствия.
    View<Npc> npcs;           ///< Все NPC.
    float halfExtent;        ///< Полусторона квадратной карты, метры.
};

/** @brief Состояние управляемого персонажа, не связанное с отображением. */
struct Player {
    Vec2 position{0.0f, -1.0f}; ///< Координаты на XZ.
    float yaw = 0.0f;           ///< Поворот в радианах, ноль соответствует +Z.
    float jumpHeight = 0.0f;    ///< Вертикальное смещение для визуального прыжка.
    float verticalSpeed = 0.0f; ///< Вертикальная скорость прыжка.
};

/** @brief Нормализованный ввод для одного обновления движения. */
struct MovementInput {
    float forward = 0.0f; ///< Вперёд / назад: [-1, 1].
    float right = 0.0f;   ///< Вправо / влево: [-1, 1].
    bool sprint = false;  ///< Увеличить скорость.
    bool jump = false;    ///< Запрос прыжка на текущем кадре.
};

/** @brief Результат поиска ближайшего объекта для взаимодействия. */
struct Interaction {
    /** @brief Тип возможной цели. */
    enum class Type : std::uint8_t { None, Npc, Resource };
    Type type = Type::None; ///< Вид цели или None.
    std::size_t index = 0;  ///< Индекс в соответствующем View, применим если type != None.
    float distanceSquared = 0.0f; ///< Квадрат расстояния до найденной цели.
};

/** @brief Параметры пользовательского интерфейса, поступающие из Application. */
struct HudState {
    Interaction nearby{};         ///< Ближайшая доступная цель.
    int activeDialogue = -1;      ///< Индекс NPC или -1, если диалог закрыт.
    bool showHelp = false;        ///< Отобразить справку.
    bool cursorReleased = false;  ///< Курсор освобождён.
    bool resourceNotice = false;  ///< Показать сообщение о неподключённой добыче.
};

/** @brief Квадрат евклидова расстояния, без вызова sqrt. */
constexpr float distance_squared(Vec2 a, Vec2 b) noexcept {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

/** @} */
} // namespace rpg3d
