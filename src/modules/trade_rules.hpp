#pragma once
/** @file trade_rules.hpp
 *  @brief Чистая проверка торговых контрактов без зависимости от хранилища.
 */
#include "api/economy_contracts.hpp"

namespace rpg3d {
/** @brief Валидация публичного контракта до обращения к транзакционному слою. */
class TradeRules final {
public:
    /** @brief Проверить допустимость актива, включая ограничения количества. */
    static bool valid_asset(const Asset& a) noexcept {
        if (a.quantity <= 0 || a.quantity > 1000000000LL) return false;
        if (a.kind == AssetKind::Resource)
            return a.id == 0 && (a.code == "ore" || a.code == "wood" || a.code == "gem");
        if (a.kind == AssetKind::Currency)
            return a.id == 0 && (a.code == "gold" || a.code == "silver");
        return a.kind == AssetKind::UniqueItem && a.code == "item" &&
               a.id > 0 && a.quantity == 1;
    }

    /** @brief Исключить бессмысленный обмен с самим собой и некорректные активы. */
    static bool valid_offer(ActorId seller, ActorId buyer,
                            const Asset& give, const Asset& take) noexcept {
        return seller > 0 && buyer > 0 && seller != buyer &&
               valid_asset(give) && valid_asset(take) &&
               !(give.kind == take.kind && give.code == take.code && give.id == take.id);
    }
    /**
     * @brief Простой торговый алгоритм NPC из первой локальной версии.
     * @note NPC не принимает произвольные цены. Это не механизм аутентификации.
     */
    static bool npc_accepts(const TradeOffer& offer) noexcept {
        if (offer.seller!=1 || offer.buyer!=2 || offer.status!="open") return false;
        const Asset& give=offer.give;
        const Asset& take=offer.take;
        const bool oreForGold=give.kind==AssetKind::Resource && give.code=="ore" &&
            give.quantity==1 && take.kind==AssetKind::Currency &&
            take.code=="gold" && take.quantity==10;
        const bool goldForSilver=give.kind==AssetKind::Currency && give.code=="gold" &&
            give.quantity==5 && take.kind==AssetKind::Currency &&
            take.code=="silver" && take.quantity==20;
        return oreForGold || goldForSilver;
    }
};
} // namespace rpg3d
