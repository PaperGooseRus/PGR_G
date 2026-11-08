/** @file test_ledger.cpp @brief Регрессия экономики, журнала и уникальных открытий. */
#include "modules/ledger_store.hpp"
#include "modules/trade_rules.hpp"
#include "modules/rare_discovery.hpp"
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

using namespace rpg3d;

static void expect(EconomyCode actual, EconomyCode wanted) { assert(actual==wanted); }
static InventorySnapshot items(LedgerStore& db, ActorId actor) {
    InventorySnapshot i;
    expect(db.inventory(actor,actor,i).code, EconomyCode::Ok);
    return i;
}
int main() {
    assert(TradeRules::valid_offer(1,2,Asset{AssetKind::Resource,"ore",1,0},
                                   Asset{AssetKind::Currency,"gold",10,0}));
    assert(!TradeRules::valid_offer(1,1,Asset{AssetKind::Resource,"ore",1,0},
                                    Asset{AssetKind::Currency,"gold",10,0}));
    const char* filename="rpg_ledger_test.sqlite";
    std::remove(filename);
    {
        LedgerStore db(filename);
        assert(db.verify().valid);
        db.register_node(13,"ore",2,20);
        db.register_node(17,"wood",2,20);
        auto first = db.harvest(1,13);
        expect(first.code,EconomyCode::Ok);
        expect(db.harvest(3,13).code,EconomyCode::Ok);
        expect(db.harvest(3,13).code,EconomyCode::Depleted);
        assert(items(db,1).ore==1 && items(db,3).ore==1);
        InventorySnapshot forbidden;
        forbidden.ore=777;
        expect(db.inventory(3,1,forbidden).code,EconomyCode::AccessDenied);
        assert(forbidden.ore==777);
        assert(db.regenerate(4102444800LL)>=1); // 2100 г. Восстанавливаются оба источника.
        expect(db.harvest(1,13).code,EconomyCode::Ok);
        assert(items(db,1).ore==2);

        Asset ore{AssetKind::Resource,"ore",1,0};
        Asset gold{AssetKind::Currency,"gold",10,0};
        auto offer=db.create_offer(1,3,ore,gold);
        expect(offer.code,EconomyCode::Ok);
        assert(items(db,1).ore==1); // Escrow зарезервирован.
        assert(db.pending_offer(1,3)==offer.id);
        TradeOffer output;
        expect(db.offer(2,offer.id,output).code,EconomyCode::AccessDenied);
        expect(db.offer(1,offer.id,output).code,EconomyCode::Ok);
        assert(!TradeRules::npc_accepts(output));
        expect(db.accept_offer(2,offer.id).code,EconomyCode::AccessDenied);
        auto accepted=db.accept_offer(3,offer.id);
        expect(accepted.code,EconomyCode::Ok);
        expect(db.accept_offer(3,offer.id).code,EconomyCode::AlreadyProcessed);
        assert(items(db,1).gold==60 && items(db,3).gold==190);
        assert(items(db,3).ore==2);

        auto cancelled=db.create_offer(3,1,Asset{AssetKind::Currency,"silver",7,0},ore);
        expect(cancelled.code,EconomyCode::Ok);
        assert(items(db,3).silver==193);
        expect(db.cancel_offer(1,cancelled.id).code,EconomyCode::AccessDenied);
        expect(db.cancel_offer(3,cancelled.id).code,EconomyCode::Ok);
        assert(items(db,3).silver==200);
        expect(db.accept_offer(1,cancelled.id).code,EconomyCode::AlreadyProcessed);

        auto swap=db.create_offer(3,1,Asset{AssetKind::Currency,"gold",5,0},
                                             Asset{AssetKind::Currency,"silver",12,0});
        expect(swap.code,EconomyCode::Ok);
        expect(db.accept_offer(1,swap.id).code,EconomyCode::Ok);
        assert(items(db,3).silver==212);
        assert(items(db,1).silver==108);
        assert(items(db,1).gold==65);

        db.register_jump(1);
        assert(db.ritual_context(1).jumps==1);
        const RareRule rule=db.rare_rule();
        assert(rule.requiredJumps>=2 && rule.requiredJumps<=5);
        assert(!RareDiscovery::eligible({},rule));
        RareContext context{true,rule.requiredJumps,rule.requiredRituals,rule.requiredDirection};
        assert(RareDiscovery::eligible(context,rule));
        expect(db.synthesize1024(1,0,false).code,EconomyCode::ConditionsNotMet);
        expect(db.synthesize1024(1,1000000,true).code,EconomyCode::ConditionsNotMet);
        expect(db.synthesize1024(1,0,true).code,EconomyCode::Insufficient);
        assert(db.discovery_count()==0);
        db.register_node(22,"ore",10,60);
        db.register_node(23,"wood",10,60);
        db.register_node(24,"gem",10,60);
        for(int n=0;n<4;++n) expect(db.harvest(1,22).code,EconomyCode::Ok);
        expect(db.harvest(1,23).code,EconomyCode::Ok);
        expect(db.harvest(1,24).code,EconomyCode::Ok);
        // Случайный исход внедряется только в тест: в игре берётся из RAND_bytes.
        expect(db.synthesize1024(1,0,true).code,EconomyCode::Ok);
        assert(db.discovery_count()==1);
        assert(items(db,1).uniqueItems.size()==1);
        const std::int64_t uniqueId=items(db,1).uniqueItems.front();
        expect(db.synthesize1024(3,0,true).code,EconomyCode::Conflict);
        auto rareTrade=db.create_offer(1,3,Asset{AssetKind::UniqueItem,"item",1,uniqueId},
                                            Asset{AssetKind::Currency,"gold",20,0});
        expect(rareTrade.code,EconomyCode::Ok);
        assert(items(db,1).uniqueItems.size()==1); // Предмет заблокирован, но не исчезает.
        expect(db.create_offer(1,2,Asset{AssetKind::UniqueItem,"item",1,uniqueId},gold).code,
               EconomyCode::Insufficient);
        expect(db.accept_offer(3,rareTrade.id).code,EconomyCode::Ok);
        assert(items(db,1).uniqueItems.empty());
        assert(items(db,3).uniqueItems.size()==1);
        assert(items(db,3).uniqueItems.front()==uniqueId);
        assert(db.verify().valid);
        assert(db.events_for(3).size()>0);
        std::cout << "Core economics: OK; events=" << db.event_count() << '\n';
    }
    {
        LedgerStore db(filename);
        assert(items(db,1).gold==85);
        assert(items(db,3).silver==212);
        assert(db.verify().valid);
    }
    // Балансы нельзя изменить в обход журнала: контрольный хеш состояния.
    sqlite3* raw=nullptr;
    assert(sqlite3_open(filename,&raw)==SQLITE_OK);
    assert(sqlite3_exec(raw,"UPDATE holdings SET amount=999 WHERE actor=1 AND code='gold'",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(raw);
    bool stateTamper=false;
    try { LedgerStore invalid(filename); (void)invalid; }
    catch (const std::runtime_error&) {stateTamper=true;}
    assert(stateTamper);
    assert(sqlite3_open(filename,&raw)==SQLITE_OK);
    assert(sqlite3_exec(raw,"UPDATE holdings SET amount=85 WHERE actor=1 AND code='gold'",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(raw);
    { LedgerStore repaired(filename); assert(repaired.verify().valid); }
    // База должна обнаруживать изменение старого события и не разрешать запуск.
    raw=nullptr;
    assert(sqlite3_open(filename,&raw)==SQLITE_OK);
    assert(sqlite3_exec(raw,"UPDATE events SET payload='TAMPER' WHERE seq=2",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(raw);
    bool detected=false;
    try { LedgerStore tampered(filename); (void)tampered; }
    catch (const std::runtime_error&) { detected=true; }
    assert(detected);
    std::remove(filename);
    std::remove("rpg_ledger_test.sqlite-wal");
    std::remove("rpg_ledger_test.sqlite-shm");
    std::cout << "Ledger tampering detected: OK\n";
}
