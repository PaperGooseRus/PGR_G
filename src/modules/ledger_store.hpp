#pragma once
/**
 * @file ledger_store.hpp
 * @brief Автономное транзакционное хранилище экономики и хешированного журнала.
 * @details Использует SQLite и OpenSSL SHA-256. Ничего не знает о raylib,
 *          3D-мире или соседних модулях. Все изменения выполняются в
 *          BEGIN IMMEDIATE/COMMIT вместе с записью событий.
 * @warning Предполагается один владелец соединения (поток симуляции).
 *          Это НЕ blockchain и не серверное разграничение доступа к файлу БД.
 */
#include "api/economy_contracts.hpp"
#include <sqlite3.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rpg3d {
/**
 * @brief Единственный владелец базы данных локального игрового мира.
 * @details Публичные команды принимают доверенный ActorId. В будущей сети
 *          идентификатор должен задаваться серверной сессией, а не клиентом.
 */
class LedgerStore final {
    struct Statement {
        sqlite3_stmt* stmt = nullptr;
        Statement(sqlite3* db, const char* sql) {
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(db));
        }
        ~Statement() { sqlite3_finalize(stmt); }
        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;
        void number(int idx, std::int64_t val) { sqlite3_bind_int64(stmt, idx, val); }
        void string(int idx, const std::string& val) {
            sqlite3_bind_text(stmt, idx, val.c_str(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
        }
        bool row() {
            const int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) return true;
            if (rc == SQLITE_DONE) return false;
            throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(stmt)));
        }
        void done() { if (row()) throw std::runtime_error("Ожидалась запись, а не строка результата"); }
        std::int64_t integer(int col) const { return sqlite3_column_int64(stmt, col); }
        std::string value(int col) const {
            const unsigned char* p = sqlite3_column_text(stmt, col);
            return p ? reinterpret_cast<const char*>(p) : "";
        }
    };

    class Transaction {
        LedgerStore& store_;
        bool committed_ = false;
    public:
        explicit Transaction(LedgerStore& store) : store_(store) { store_.exec("BEGIN IMMEDIATE"); }
        ~Transaction() { if (!committed_) { sqlite3_exec(store_.db_, "ROLLBACK", nullptr, nullptr, nullptr); } }
        void commit() { store_.exec("COMMIT"); committed_ = true; }
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
    };

public:
    /**
     * @brief Открыть/создать SQLite-файл, включить WAL и установить схему.
     * @param filename Путь к БД; ":memory:" допускается для тестов.
     * @throws std::runtime_error При SQL-ошибке или повреждённой цепочке.
     * @warning Не передавайте путь клиента напрямую по сети.
     */
    explicit LedgerStore(const std::string& filename) {
        if (sqlite3_open_v2(filename.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                            SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
            const std::string message = db_ ? sqlite3_errmsg(db_) : "SQLite open failed";
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
            throw std::runtime_error(message);
        }
        try {
            sqlite3_busy_timeout(db_, 5000);
            exec("PRAGMA foreign_keys=ON");
            exec("PRAGMA journal_mode=WAL");
            exec("PRAGMA synchronous=FULL");
            schema();
            bootstrap();
            const VerifyResult check = verify();
            if (!check.valid) throw std::runtime_error("Нарушена цепочка событий (запись №" +
                std::to_string(check.firstInvalid) + "). База открыта только для восстановления из резервной копии.");
        } catch (...) {
            sqlite3_close(db_);
            db_ = nullptr;
            throw;
        }
    }
    ~LedgerStore() { if (db_) sqlite3_close(db_); }
    LedgerStore(const LedgerStore&) = delete;
    LedgerStore& operator=(const LedgerStore&) = delete;

    /** @brief Проверить всю цепочку SHA-256 с нулевого блока до последнего. */
    VerifyResult verify() const {
        Statement st(db_, "SELECT seq,utc,type,payload,visibility,actor,counterparty,prev_hash,hash,state_hash "
                          "FROM events ORDER BY seq");
        VerifyResult result{true,0,0};
        std::string previous(64, '0');
        std::string latestState;
        while (st.row()) {
            const std::int64_t seq = st.integer(0);
            const std::string storedPrevious = st.value(7);
            const std::string storedHash = st.value(8);
            if (seq != result.checked + 1 || storedPrevious != previous ||
                storedHash != hash_event(seq, st.integer(1), st.value(2), st.value(3),
                                         st.value(4), st.integer(5), st.integer(6),
                                         previous, st.value(9))) {
                result.valid = false;
                result.firstInvalid = seq;
                return result;
            }
            ++result.checked;
            previous = storedHash;
            latestState=st.value(9);
        }
        if (!result.checked || latestState!=state_hash()) {
            result.valid=false;
            result.firstInvalid=result.checked;
            return result;
        }
        // Зафиксированное в genesis правило нельзя незаметно заменить в meta.
        Statement rule(db_,"SELECT value FROM meta WHERE key='rare_rule'");
        Statement nonce(db_,"SELECT value FROM meta WHERE key='rare_nonce'");
        Statement genesis(db_,"SELECT payload FROM events WHERE seq=1");
        if (!rule.row() || !nonce.row() || !genesis.row() ||
            genesis.value(0).find(";rare_commit="+sha256_text(rule.value(0)+":"+nonce.value(0)))==std::string::npos) {
            result.valid=false;result.firstInvalid=1;
        }
        return result;
    }

    /**
     * @brief Создать природное месторождение при первой загрузке карты.
     * @param id Стабильный индекс объекта карты + 1.
     * @param material Код добываемого ресурса: ore, wood или gem.
     * @param capacity Максимальное количество единиц.
     * @param regenSeconds Период восстановления одной единицы (секунды).
     * @note Повторный вызов не сбрасывает уже израсходованный запас.
     */
    void register_node(std::int64_t id, const std::string& material,
                       std::int64_t capacity, std::int64_t regenSeconds) {
        if (id <= 0 || capacity <= 0 || regenSeconds <= 0 ||
            (material != "ore" && material != "wood" && material != "gem"))
            throw std::invalid_argument("Неверные параметры месторождения");
        Transaction tx(*this);
        Statement st(db_, "INSERT OR IGNORE INTO nodes(id,material,stock,capacity,regen_sec,last_regen) "
                          "VALUES(?,?,?,?,?,?)");
        st.number(1,id); st.string(2,material); st.number(3,capacity);
        st.number(4,capacity); st.number(5,regenSeconds); st.number(6,utc_now());
        st.done();
        if (sqlite3_changes(db_)==1)
            append("node_init","node="+std::to_string(id)+";material="+material+
                   ";capacity="+std::to_string(capacity),"public",0,0);
        tx.commit();
    }

    /** @brief Доступный всем снимок состояния природного месторождения. */
    NodeSnapshot node(std::int64_t id) const {
        Statement st(db_, "SELECT material,stock,capacity FROM nodes WHERE id=?");
        st.number(1,id);
        if (!st.row()) return {};
        return {id,st.value(0),st.integer(1),st.integer(2)};
    }

    /**
     * @brief Атомарно добыть ресурс общего мира и зачислить в личный инвентарь.
     * @return Depleted, если единицы больше нет; Success изменяет оба состояния.
     * @warning caller обязан проверять расстояние до месторождения в симуляции.
     */
    EconomyResult harvest(ActorId actor, std::int64_t nodeId) {
        if (!account_exists(actor)) return {EconomyCode::AccessDenied,"Неизвестный персонаж"};
        Transaction tx(*this);
        const NodeSnapshot source = node(nodeId);
        if (source.id == 0) return {EconomyCode::NotFound,"Месторождение отсутствует"};
        if (source.remaining <= 0) return {EconomyCode::Depleted,"Ресурс исчерпан"};
        Statement decrease(db_, "UPDATE nodes SET stock=stock-1 WHERE id=? AND stock>0");
        decrease.number(1,nodeId); decrease.done();
        if (sqlite3_changes(db_) != 1) return {EconomyCode::Depleted,"Ресурс уже добыт"};
        credit(actor, Asset{AssetKind::Resource,source.material,1,0});
        bool bonusGem = false;
        if (source.material == "ore") {
            unsigned char sample = 0;
            if (RAND_bytes(&sample,1) != 1) throw std::runtime_error("RAND_bytes failed");
            bonusGem = sample < 32; // вероятность 12,5%, без округления.
            if (bonusGem) credit(actor,Asset{AssetKind::Resource,"gem",1,0});
        }
        const auto seq = append("harvest", "node="+std::to_string(nodeId)+";kind="+source.material+
            (bonusGem ? ";gem=1" : ""), "private", actor, 0);
        tx.commit();
        return {EconomyCode::Ok,bonusGem ? "Добыто сырьё и найден кристалл" : "Ресурс добыт",seq};
    }

    /**
     * @brief Восстановить ресурсы по реальному времени без потока на каждое дерево.
     * @param nowUnixSeconds Текущее время UTC; можно внедрять тестовое значение.
     * @return Число обновлённых месторождений, без аллокаций при отсутствии событий.
     */
    std::int64_t regenerate(std::int64_t nowUnixSeconds) {
        Transaction tx(*this);
        Statement st(db_, "UPDATE nodes SET "
            "stock=min(capacity,stock+(?-last_regen)/regen_sec), "
            "last_regen=last_regen+((?-last_regen)/regen_sec)*regen_sec "
            "WHERE ? >= last_regen+regen_sec");
        st.number(1,nowUnixSeconds); st.number(2,nowUnixSeconds); st.number(3,nowUnixSeconds);
        st.done();
        const std::int64_t count = sqlite3_changes(db_);
        if (count > 0) append("regenerate", "nodes="+std::to_string(count), "public", 0,0);
        tx.commit();
        return count;
    }

    /**
     * @brief Получить приватный инвентарь; доступен только owner == observer.
     * @param observer Идентификатор авторизованного наблюдателя.
     * @param owner Идентификатор владельца.
     * @param out Заполняется только при успехе; иначе содержимое не изменяется.
     */
    EconomyResult inventory(ActorId observer, ActorId owner, InventorySnapshot& out) const {
        if (observer != owner || !account_exists(observer))
            return {EconomyCode::AccessDenied,"Чужой инвентарь закрыт"};
        InventorySnapshot temporary{}; temporary.owner=owner;
        Statement st(db_, "SELECT code,amount FROM holdings WHERE actor=?");
        st.number(1,owner);
        while (st.row()) {
            const std::string key = st.value(0); const auto n=st.integer(1);
            if (key=="ore") temporary.ore=n;
            else if (key=="wood") temporary.wood=n;
            else if (key=="gem") temporary.gem=n;
            else if (key=="gold") temporary.gold=n;
            else if (key=="silver") temporary.silver=n;
        }
        Statement items(db_, "SELECT id FROM unique_items WHERE owner=? ORDER BY id");
        items.number(1,owner);
        while (items.row()) temporary.uniqueItems.push_back(items.integer(0));
        out = std::move(temporary);
        return {EconomyCode::Ok,"Инвентарь получен"};
    }

    /**
     * @brief Создать сделку и зарезервировать предлагаемый актив.
     * @details Предложение даёт только owner ресурса. Окончательный обмен
     *          проводится методом accept_offer одной транзакцией.
     */
    EconomyResult create_offer(ActorId seller,ActorId buyer,const Asset& give,const Asset& take) {
        if (seller<=0 || buyer<=0 || seller==buyer || !valid(give) || !valid(take) ||
            (give.kind==take.kind && give.code==take.code && give.id==take.id))
            return {EconomyCode::Invalid,"Недопустимые условия сделки"};
        if (!account_exists(seller) || !account_exists(buyer))
            return {EconomyCode::NotFound,"Участник сделки не существует"};
        Transaction tx(*this);
        if (!reserve(seller,give)) return {EconomyCode::Insufficient,"У продавца недостаточно имущества"};
        Statement st(db_, "INSERT INTO offers(seller,buyer,give_kind,give_code,give_qty,give_id,take_kind,take_code,take_qty,take_id,status) "
                          "VALUES(?,?,?,?,?,?,?,?,?,?,'open')");
        st.number(1,seller); st.number(2,buyer); st.number(3,static_cast<int>(give.kind));
        st.string(4,give.code); st.number(5,give.quantity); st.number(6,give.id);
        st.number(7,static_cast<int>(take.kind)); st.string(8,take.code);
        st.number(9,take.quantity); st.number(10,take.id); st.done();
        const std::int64_t id=sqlite3_last_insert_rowid(db_);
        if (give.kind==AssetKind::UniqueItem) {
            Statement link(db_, "UPDATE unique_items SET reserved_offer=? WHERE id=? AND owner=? AND reserved_offer=-1");
            link.number(1,id);link.number(2,give.id);link.number(3,seller);link.done();
            if (sqlite3_changes(db_)!=1) throw std::runtime_error("Reserve link failed");
        }
        append("offer_open",describe_trade(id,seller,buyer,give,take),"participants",seller,buyer);
        tx.commit();
        return {EconomyCode::Ok,"Предложение создано, актив зарезервирован",id};
    }

    /** @brief Одним COMMIT обменять оба актива; повторный вызов ничего не выдаёт. */
    EconomyResult accept_offer(ActorId actor,std::int64_t offerId) {
        Transaction tx(*this);
        const auto offer=offer_by_id(offerId);
        if (offer.id==0) return {EconomyCode::NotFound,"Предложение не найдено"};
        if (actor!=offer.buyer) return {EconomyCode::AccessDenied,"Только покупатель может принять условия"};
        if (offer.status!="open") return {EconomyCode::AlreadyProcessed,"Предложение уже завершено"};
        if (!debit(actor,offer.take)) return {EconomyCode::Insufficient,"Недостаточно средств для обмена"};
        if (offer.take.kind!=AssetKind::UniqueItem) credit(offer.seller,offer.take);
        else transfer_reserved_item(offer.take.id,actor,offer.seller,0);
        if (offer.give.kind!=AssetKind::UniqueItem) credit(actor,offer.give);
        else transfer_reserved_item(offer.give.id,offer.seller,actor,offerId);
        Statement update(db_,"UPDATE offers SET status='done' WHERE id=? AND status='open'");
        update.number(1,offerId);update.done();
        if (sqlite3_changes(db_)!=1) throw std::runtime_error("Offer changed during acceptance");
        const std::int64_t receipt=append("offer_done",describe_trade(offer.id,offer.seller,offer.buyer,
            offer.give,offer.take),"participants",offer.seller,offer.buyer);
        tx.commit();
        return {EconomyCode::Ok,"Обмен завершён, квитанция в журнале",receipt};
    }

    /** @brief Отменить предложение продавцом и вернуть весь резерв. */
    EconomyResult cancel_offer(ActorId actor,std::int64_t offerId) {
        Transaction tx(*this);
        const auto offer=offer_by_id(offerId);
        if (!offer.id) return {EconomyCode::NotFound,"Предложение не найдено"};
        if (actor!=offer.seller) return {EconomyCode::AccessDenied,"Только продавец может отменить предложение"};
        if (offer.status!="open") return {EconomyCode::AlreadyProcessed,"Предложение уже завершено"};
        if (offer.give.kind!=AssetKind::UniqueItem) credit(actor,offer.give);
        else {
            Statement item(db_,"UPDATE unique_items SET reserved_offer=0 WHERE id=? AND owner=? AND reserved_offer=?");
            item.number(1,offer.give.id);item.number(2,actor);item.number(3,offerId);item.done();
            if (sqlite3_changes(db_)!=1) throw std::runtime_error("Escrow item not found");
        }
        Statement st(db_,"UPDATE offers SET status='cancelled' WHERE id=? AND status='open'");
        st.number(1,offerId);st.done();
        append("offer_cancel", "offer="+std::to_string(offerId),"participants",actor,offer.buyer);
        tx.commit();
        return {EconomyCode::Ok,"Предложение отменено, резерв возвращён"};
    }

    /**
     * @brief Восстановить последний незавершённый заказ NPC после перезапуска.
     * @param seller Идентификатор владельца зарезервированного актива.
     * @param buyer Будущий получатель актива.
     * @return ID самого раннего активного заказа либо 0.
     */
    std::int64_t pending_offer(ActorId seller,ActorId buyer) const {
        Statement st(db_,"SELECT id FROM offers WHERE seller=? AND buyer=? "
                         "AND status='open' ORDER BY id LIMIT 1");
        st.number(1,seller);st.number(2,buyer);
        return st.row()?st.integer(0):0;
    }

    /** @brief Получить предложение только его участникам. */
    EconomyResult offer(ActorId viewer,std::int64_t id,TradeOffer& out) const {
        const TradeOffer temp=offer_by_id(id);
        if (!temp.id) return {EconomyCode::NotFound,"Предложение не найдено"};
        if (temp.seller!=viewer && temp.buyer!=viewer)
            return {EconomyCode::AccessDenied,"Чужие условия сделки закрыты"};
        out=temp;
        return {EconomyCode::Ok,"Предложение найдено"};
    }

    /**
     * @brief Записать акт молитвы не чаще раза за игровой день (45 реальных секунд).
     * @details Данные переживают перезапуск; без бесконечной накрутки кнопкой.
     */
    EconomyResult meditate(ActorId actor,std::int64_t nowSeconds) {
        if (!account_exists(actor)) return {EconomyCode::AccessDenied,"Неизвестный персонаж"};
        const auto day=std::max<std::int64_t>(0,(nowSeconds-world_start())/45);
        Transaction tx(*this);
        Statement st(db_,"UPDATE rituals SET ritual_days=ritual_days+1,last_day=? "
                         "WHERE actor=? AND last_day<?");
        st.number(1,day);st.number(2,actor);st.number(3,day);st.done();
        if (sqlite3_changes(db_)!=1)
            return {EconomyCode::Conflict,"Сегодня молитва уже совершена"};
        append("meditate","day="+std::to_string(day),"private",actor,0);
        tx.commit();
        return {EconomyCode::Ok,"Молитва засчитана. Следующий игровой день через 45 секунд"};
    }

    /** @brief Зафиксировать подтверждённый прыжок; вызывать только после физики. */
    void register_jump(ActorId actor) {
        Transaction tx(*this);
        Statement st(db_,"UPDATE rituals SET jumps=min(jumps+1,1000000) WHERE actor=? AND jumps<1000000");
        st.number(1,actor);st.done();
        if (sqlite3_changes(db_)==1)
            append("jump","verified_jump=1","private",actor,0);
        tx.commit();
    }

    /** @brief Получить накопленные ритуальные условия (публичен только самому владельцу). */
    RareContext ritual_context(ActorId actor) const {
        Statement st(db_,"SELECT jumps,ritual_days FROM rituals WHERE actor=?");
        st.number(1,actor);
        RareContext ctx{};
        if (st.row()) {
            ctx.jumps=static_cast<unsigned>(st.integer(0));
            ctx.rituals=static_cast<unsigned>(st.integer(1));
        }
        return ctx;
    }

    /** @brief Получить скрытое правило мира для доверенного слоя симуляции. */
    RareRule rare_rule() const {
        Statement st(db_,"SELECT value FROM meta WHERE key='rare_rule'");
        if (!st.row()) throw std::runtime_error("Не задано правило открытий");
        const std::string s=st.value(0);
        unsigned j=0,p=0,d=0;
        if (std::sscanf(s.c_str(),"%u:%u:%u",&j,&p,&d)!=3)
            throw std::runtime_error("Повреждено правило мира");
        return {j,p,d};
    }

    /**
     * @brief Выполнить попытку синтеза элемента №1024 после проверки условий.
     * @param roll Доверенное число 0..999999, полученное серверной симуляцией.
     * @param prerequisitesMet Результат внешней проверки правила мира.
     * @return failedRoll означает расход реагентов и неудачу; Success — 1 предмет.
     * @warning Клиент не должен сам выбирать roll/prerequisitesMet в мультиплеере.
     */
    EconomyResult synthesize1024(ActorId actor,std::uint32_t roll,bool prerequisitesMet) {
        if (!account_exists(actor)) return {EconomyCode::AccessDenied,"Неизвестный персонаж"};
        if (!prerequisitesMet || roll>=1000000U)
            return {EconomyCode::ConditionsNotMet,"Скрытые условия не выполнены"};
        Transaction tx(*this);
        if (has_discovery("element:1024"))
            return {EconomyCode::Conflict,"Элемент 1024 уже существует в этом мире"};
        const Asset ore{AssetKind::Resource,"ore",2,0};
        const Asset wood{AssetKind::Resource,"wood",1,0};
        const Asset gem{AssetKind::Resource,"gem",1,0};
        if (!has(actor,ore) || !has(actor,wood) || !has(actor,gem))
            return {EconomyCode::Insufficient,"Нужны: 2 руды, древесина и кристалл"};
        if (!debit(actor,ore) || !debit(actor,wood) || !debit(actor,gem))
            throw std::runtime_error("Inconsistent synthesis debit");
        if (roll!=0) {
            append("synthesis_failed","element=1024;consumed=ore2,wood1,gem1;roll="+
                   std::to_string(roll),"private",actor,0);
            tx.commit();
            return {EconomyCode::FailedRoll,"Синтез не удался; реагенты израсходованы"};
        }
        Statement item(db_,"INSERT INTO unique_items(kind,owner,reserved_offer,origin_event) VALUES('element:1024',?,0,0)");
        item.number(1,actor);item.done();
        const std::int64_t itemId=sqlite3_last_insert_rowid(db_);
        const auto eventId=event_count()+1;
        Statement discovery(db_,"INSERT INTO discoveries(key,first_actor,item_id,event_id) VALUES('element:1024',?,?,?)");
        discovery.number(1,actor);discovery.number(2,itemId);discovery.number(3,eventId);discovery.done();
        Statement itemEvent(db_,"UPDATE unique_items SET origin_event=? WHERE id=?");
        itemEvent.number(1,eventId);itemEvent.number(2,itemId);itemEvent.done();
        const auto actualEvent=append("discovery","element=1024;item="+std::to_string(itemId)+
                            ";first=1;chance=1/1000000","public",actor,0);
        if (eventId!=actualEvent) throw std::runtime_error("Event sequence mismatch");
        tx.commit();
        return {EconomyCode::Ok,"Открытие! Создан единственный элемент №1024",itemId};
    }

    /** @brief Получить видимые конкретному наблюдателю события, последние первыми. */
    std::vector<EventRecord> events_for(ActorId observer,std::size_t maxCount=12) const {
        if (!account_exists(observer)) return {};
        Statement st(db_,"SELECT seq,type,payload,hash FROM events WHERE visibility='public' OR "
                         "actor=? OR counterparty=? ORDER BY seq DESC LIMIT ?");
        st.number(1,observer); st.number(2,observer);
        st.number(3,static_cast<std::int64_t>(std::min<std::size_t>(maxCount,100)));
        std::vector<EventRecord> out;
        while(st.row()) out.push_back({st.integer(0),st.value(1),st.value(2),st.value(3)});
        return out;
    }

    /** @brief Количество публично подтверждённых открытий элемента. */
    std::int64_t discovery_count() const {
        Statement st(db_,"SELECT COUNT(*) FROM discoveries WHERE key='element:1024'");
        return st.row()?st.integer(0):0;
    }

    /** @brief Текущее число записей журнала, используется для вывода статуса. */
    std::int64_t event_count() const {
        Statement st(db_,"SELECT COUNT(*) FROM events");
        return st.row()?st.integer(0):0;
    }

private:
    sqlite3* db_ = nullptr; ///< Соединение с единственным владельцем.

    static std::int64_t utc_now() {
        using namespace std::chrono;
        return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    }
    void exec(const char* sql) const {
        char* err=nullptr;
        if (sqlite3_exec(db_,sql,nullptr,nullptr,&err)!=SQLITE_OK) {
            const std::string why=err?err:"Ошибка SQLite";
            sqlite3_free(err);
            throw std::runtime_error(why);
        }
    }
    void schema() {
        exec("CREATE TABLE IF NOT EXISTS accounts(id INTEGER PRIMARY KEY,display_name TEXT NOT NULL)");
        exec("CREATE TABLE IF NOT EXISTS holdings(actor INTEGER NOT NULL REFERENCES accounts(id),code TEXT NOT NULL," 
             "amount INTEGER NOT NULL CHECK(amount>=0 AND amount<=1000000000000),PRIMARY KEY(actor,code))");
        exec("CREATE TABLE IF NOT EXISTS nodes(id INTEGER PRIMARY KEY,material TEXT NOT NULL," 
             "stock INTEGER NOT NULL CHECK(stock>=0),capacity INTEGER NOT NULL CHECK(capacity>0)," 
             "regen_sec INTEGER NOT NULL CHECK(regen_sec>0),last_regen INTEGER NOT NULL)");
        exec("CREATE TABLE IF NOT EXISTS offers(id INTEGER PRIMARY KEY AUTOINCREMENT," 
             "seller INTEGER NOT NULL REFERENCES accounts(id),buyer INTEGER NOT NULL REFERENCES accounts(id)," 
             "give_kind INTEGER NOT NULL,give_code TEXT NOT NULL,give_qty INTEGER NOT NULL,give_id INTEGER NOT NULL," 
             "take_kind INTEGER NOT NULL,take_code TEXT NOT NULL,take_qty INTEGER NOT NULL,take_id INTEGER NOT NULL," 
             "status TEXT NOT NULL CHECK(status IN ('open','done','cancelled')))");
        exec("CREATE TABLE IF NOT EXISTS unique_items(id INTEGER PRIMARY KEY AUTOINCREMENT," 
             "kind TEXT NOT NULL,owner INTEGER NOT NULL REFERENCES accounts(id)," 
             "reserved_offer INTEGER NOT NULL DEFAULT 0,origin_event INTEGER NOT NULL DEFAULT 0)");
        exec("CREATE TABLE IF NOT EXISTS discoveries(key TEXT PRIMARY KEY,first_actor INTEGER NOT NULL," 
             "item_id INTEGER NOT NULL UNIQUE,event_id INTEGER NOT NULL UNIQUE)");
        exec("CREATE TABLE IF NOT EXISTS events(seq INTEGER PRIMARY KEY,utc INTEGER NOT NULL," 
             "type TEXT NOT NULL,payload TEXT NOT NULL,visibility TEXT NOT NULL," 
             "actor INTEGER NOT NULL,counterparty INTEGER NOT NULL,prev_hash TEXT NOT NULL," 
             "hash TEXT NOT NULL,state_hash TEXT NOT NULL)");
        exec("CREATE TABLE IF NOT EXISTS rituals(actor INTEGER PRIMARY KEY REFERENCES accounts(id)," 
             "jumps INTEGER NOT NULL DEFAULT 0,ritual_days INTEGER NOT NULL DEFAULT 0,last_day INTEGER NOT NULL DEFAULT -1)");
        exec("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL)");
    }
    void bootstrap() {
        Statement st(db_,"SELECT COUNT(*) FROM accounts");
        if (st.row() && st.integer(0)>0) return;
        Transaction tx(*this);
        exec("INSERT INTO accounts(id,display_name) VALUES(1,'Игрок'),(2,'NPC: торговец'),(3,'Второй игрок (тест)')");
        exec("INSERT INTO holdings(actor,code,amount) VALUES "
             "(1,'gold',50),(1,'silver',120),(2,'gold',500),(2,'silver',500)," 
             "(3,'gold',200),(3,'silver',200)");
        exec("INSERT INTO rituals(actor) VALUES(1),(2),(3)");
        {
            Statement start(db_,"INSERT INTO meta(key,value) VALUES('world_start',?)");
            start.string(1,std::to_string(utc_now())); start.done();
        }
        std::array<unsigned char,19> secret{};
        if (RAND_bytes(secret.data(),static_cast<int>(secret.size()))!=1)
            throw std::runtime_error("RAND_bytes for world secret failed");
        const std::string hiddenRule=std::to_string(2U+secret[0]%4U)+":"+
              std::to_string(3U+secret[1]%4U)+":"+
              std::to_string(secret[2]%4U);
        {
            Statement rule(db_,"INSERT INTO meta(key,value) VALUES('rare_rule',?)");
            rule.string(1,hiddenRule);rule.done();
        }
        std::string nonce(32,'0');
        static constexpr char hex[]="0123456789abcdef";
        for (std::size_t i=3;i<secret.size();++i) {
            nonce[2*(i-3)]=hex[secret[i]>>4];
            nonce[2*(i-3)+1]=hex[secret[i]&15U];
        }
        {
            Statement salt(db_,"INSERT INTO meta(key,value) VALUES('rare_nonce',?)");
            salt.string(1,nonce);salt.done();
        }
        append("genesis","world_created;actors=3;currency=gold,silver;rare_commit="+
               sha256_text(hiddenRule+":"+nonce),"public",0,0);
        tx.commit();
    }
    std::int64_t world_start() const {
        Statement st(db_,"SELECT value FROM meta WHERE key='world_start'");
        return st.row()?std::stoll(st.value(0)):utc_now();
    }
    bool account_exists(ActorId actor) const {
        Statement st(db_,"SELECT 1 FROM accounts WHERE id=?");st.number(1,actor);
        return st.row();
    }
    static bool valid(const Asset& a) {
        if (a.quantity<=0 || a.quantity>1000000000LL) return false;
        if (a.kind==AssetKind::Resource) return a.id==0 && (a.code=="ore" || a.code=="wood" || a.code=="gem");
        if (a.kind==AssetKind::Currency) return a.id==0 && (a.code=="gold" || a.code=="silver");
        return a.kind==AssetKind::UniqueItem && a.code=="item" && a.id>0 && a.quantity==1;
    }
    bool has(ActorId actor,const Asset& a) const {
        if (a.kind==AssetKind::UniqueItem) {
            Statement st(db_,"SELECT 1 FROM unique_items WHERE id=? AND owner=? AND reserved_offer=0");
            st.number(1,a.id);st.number(2,actor);return st.row();
        }
        Statement st(db_,"SELECT 1 FROM holdings WHERE actor=? AND code=? AND amount>=?");
        st.number(1,actor);st.string(2,a.code);st.number(3,a.quantity);return st.row();
    }
    bool debit(ActorId actor,const Asset& a) {
        if (a.kind==AssetKind::UniqueItem) return has(actor,a);
        Statement st(db_,"UPDATE holdings SET amount=amount-? WHERE actor=? AND code=? AND amount>=?");
        st.number(1,a.quantity);st.number(2,actor);st.string(3,a.code);st.number(4,a.quantity);st.done();
        return sqlite3_changes(db_)==1;
    }
    void credit(ActorId actor,const Asset& a) {
        if (a.kind==AssetKind::UniqueItem) throw std::invalid_argument("Unique item requires ownership update");
        Statement st(db_,"INSERT INTO holdings(actor,code,amount) VALUES(?,?,?) "
             "ON CONFLICT(actor,code) DO UPDATE SET amount=amount+excluded.amount");
        st.number(1,actor);st.string(2,a.code);st.number(3,a.quantity);st.done();
    }
    bool reserve(ActorId actor,const Asset& a) {
        if (a.kind!=AssetKind::UniqueItem) return debit(actor,a);
        Statement st(db_,"UPDATE unique_items SET reserved_offer=-1 WHERE id=? AND owner=? AND reserved_offer=0");
        st.number(1,a.id);st.number(2,actor);st.done();
        return sqlite3_changes(db_)==1;
    }
    void transfer_reserved_item(std::int64_t id,ActorId oldOwner,ActorId newOwner,
                                std::int64_t reserved) {
        Statement st(db_,"UPDATE unique_items SET owner=?,reserved_offer=0 WHERE id=? AND owner=? AND reserved_offer=?");
        st.number(1,newOwner);st.number(2,id);st.number(3,oldOwner);st.number(4,reserved);st.done();
        if (sqlite3_changes(db_)!=1) throw std::runtime_error("Не удалось передать уникальный предмет");
    }
    TradeOffer offer_by_id(std::int64_t id) const {
        Statement st(db_,"SELECT seller,buyer,give_kind,give_code,give_qty,give_id," 
                         "take_kind,take_code,take_qty,take_id,status FROM offers WHERE id=?");
        st.number(1,id);
        if (!st.row()) return {};
        return {id,st.integer(0),st.integer(1),
                Asset{static_cast<AssetKind>(st.integer(2)),st.value(3),st.integer(4),st.integer(5)},
                Asset{static_cast<AssetKind>(st.integer(6)),st.value(7),st.integer(8),st.integer(9)},
                st.value(10)};
    }
    bool has_discovery(const std::string& key) const {
        Statement st(db_,"SELECT 1 FROM discoveries WHERE key=?");st.string(1,key);return st.row();
    }
    static std::string describe_trade(std::int64_t id,ActorId seller,ActorId buyer,
                                      const Asset& give,const Asset& take) {
        return "offer="+std::to_string(id)+";seller="+std::to_string(seller)+
            ";buyer="+std::to_string(buyer)+";give="+give.code+":"+
            std::to_string(give.id?give.id:give.quantity)+";take="+take.code+":"+
            std::to_string(take.id?take.id:take.quantity);
    }
    static void push_hash_field(std::string& into,const std::string& s) {
        into+=std::to_string(s.size());into.push_back(':');into+=s;
    }
    static std::string sha256_text(const std::string& data) {
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(data.data()),data.size(),digest);
        static constexpr char hex[]="0123456789abcdef";
        std::string result(64,'0');
        for (std::size_t i=0;i<SHA256_DIGEST_LENGTH;++i) {
            result[i*2]=hex[digest[i]>>4];result[i*2+1]=hex[digest[i]&15U];
        }
        return result;
    }
    /** @brief Хеш материального состояния мира и ритуалов после каждой команды. */
    std::string state_hash() const {
        static constexpr const char* queries[]={
            "SELECT id,display_name FROM accounts ORDER BY id",
            "SELECT actor,code,amount FROM holdings ORDER BY actor,code",
            "SELECT id,material,stock,capacity,regen_sec,last_regen FROM nodes ORDER BY id",
            "SELECT id,seller,buyer,give_kind,give_code,give_qty,give_id,take_kind,take_code,take_qty,take_id,status FROM offers ORDER BY id",
            "SELECT id,kind,owner,reserved_offer,origin_event FROM unique_items ORDER BY id",
            "SELECT key,first_actor,item_id,event_id FROM discoveries ORDER BY key",
            "SELECT actor,jumps,ritual_days,last_day FROM rituals ORDER BY actor",
            "SELECT key,value FROM meta ORDER BY key"
        };
        std::string state="rpg-state-v1:";
        for (const char* sql:queries) {
            push_hash_field(state,sql);
            Statement st(db_,sql);
            const int columns=sqlite3_column_count(st.stmt);
            while (st.row()) {
                state.push_back('R');
                for (int c=0;c<columns;++c) push_hash_field(state,st.value(c));
            }
        }
        return sha256_text(state);
    }

    static std::string hash_event(std::int64_t seq,std::int64_t utc,const std::string& type,
                                  const std::string& payload,const std::string& visibility,
                                  ActorId actor,ActorId counterparty,const std::string& previous,
                                  const std::string& stateHash) {
        std::string data="rpg-ledger-v1:"+std::to_string(seq)+":"+std::to_string(utc)+":"+
            std::to_string(actor)+":"+std::to_string(counterparty)+":";
        push_hash_field(data,type);push_hash_field(data,payload);
        push_hash_field(data,visibility);push_hash_field(data,previous);
        push_hash_field(data,stateHash);
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(data.data()),data.size(),digest);
        static constexpr char hex[]="0123456789abcdef";
        std::string result(64,'0');
        for (std::size_t i=0;i<SHA256_DIGEST_LENGTH;++i) {
            result[i*2]=hex[digest[i]>>4];result[i*2+1]=hex[digest[i]&15U];
        }
        return result;
    }
    std::int64_t append(const std::string& type,const std::string& payload,
                        const std::string& visibility,ActorId actor,ActorId counterparty) {
        std::int64_t seq=1;
        std::string prev(64,'0');
        {
            Statement prior(db_,"SELECT seq,hash FROM events ORDER BY seq DESC LIMIT 1");
            if (prior.row()) { seq=prior.integer(0)+1;prev=prior.value(1); }
        }
        const auto utc=utc_now();
        const std::string stateHash=state_hash();
        const std::string digest=hash_event(seq,utc,type,payload,visibility,actor,counterparty,prev,stateHash);
        Statement insert(db_,"INSERT INTO events(seq,utc,type,payload,visibility,actor,counterparty,prev_hash,hash,state_hash) "
                             "VALUES(?,?,?,?,?,?,?,?,?,?)");
        insert.number(1,seq);insert.number(2,utc);insert.string(3,type);insert.string(4,payload);
        insert.string(5,visibility);insert.number(6,actor);insert.number(7,counterparty);
        insert.string(8,prev);insert.string(9,digest);insert.string(10,stateHash);insert.done();
        return seq;
    }
};
} // namespace rpg3d
