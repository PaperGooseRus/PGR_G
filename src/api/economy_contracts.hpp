#pragma once
/**
 * @file economy_contracts.hpp
 * @brief Общие контракты экономики: не зависят от SQLite, raylib и других модулей.
 */
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rpg3d {

/** @brief Устойчивый идентификатор действующего лица. 1 — локальный игрок. */
using ActorId = std::int64_t;

/** @brief Код результата команды; ожидаемые ошибки не бросают исключений. */
enum class EconomyCode : std::uint8_t {
    Ok, Invalid, AccessDenied, Insufficient, Depleted, NotFound, Conflict,
    AlreadyProcessed, ConditionsNotMet, FailedRoll
};

/** @brief Результат операции с русским диагностическим сообщением. */
struct EconomyResult {
    EconomyCode code = EconomyCode::Ok;
    std::string message{};
    std::int64_t id = 0; ///< ID созданного предложения/предмета/события, если имеется.
    explicit operator bool() const noexcept { return code == EconomyCode::Ok; }
};

/** @brief Вид активов. Для уникальных предметов quantity должен быть равен 1. */
enum class AssetKind : std::uint8_t { Resource, Currency, UniqueItem };

/**
 * @brief Актив в производстве и торговле.
 * @details code: ore, wood, gem, gold, silver; для UniqueItem — код "item" и id.
 */
struct Asset {
    AssetKind kind = AssetKind::Resource;
    std::string code = "ore";
    std::int64_t quantity = 1;
    std::int64_t id = 0;
};

/** @brief Снимок личного инвентаря; выдаётся только владельцу. */
struct InventorySnapshot {
    ActorId owner = 0;
    std::int64_t ore = 0;
    std::int64_t wood = 0;
    std::int64_t gem = 0;
    std::int64_t gold = 0;
    std::int64_t silver = 0;
    std::vector<std::int64_t> uniqueItems{};
};

/** @brief Снимок предложения: зарезервированное имущество seller -> buyer. */
struct TradeOffer {
    std::int64_t id = 0;
    ActorId seller = 0;
    ActorId buyer = 0;
    Asset give{};
    Asset take{};
    std::string status{};
};

/** @brief Видимое конкретному персонажу событие из журнала. */
struct EventRecord {
    std::int64_t sequence = 0;
    std::string type;
    std::string payload;
    std::string hash;
};

/** @brief Проверка цепочки событий, включая номер первой повреждённой записи. */
struct VerifyResult {
    bool valid = false;
    std::int64_t checked = 0;
    std::int64_t firstInvalid = 0;
};

/** @brief Положение независимого NPC, таймеры и сырьё хранятся отдельно от клиента. */
struct NodeSnapshot {
    std::int64_t id = 0;
    std::string material;
    std::int64_t remaining = 0;
    std::int64_t capacity = 0;
};

/** @brief Проверяемые входные условия редкого синтеза. */
struct RareContext {
    bool atLaboratory = false;
    unsigned jumps = 0;
    unsigned rituals = 0;
    unsigned direction = 0; ///< Сектор направления взгляда: 0..3.
};

/** @brief Условия, генерируемые отдельно для каждого игрового мира. */
struct RareRule {
    unsigned requiredJumps = 3;
    unsigned requiredRituals = 5;
    unsigned requiredDirection = 0;
};

} // namespace rpg3d
