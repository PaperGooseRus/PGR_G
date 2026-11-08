#pragma once

#include "game_state.hpp"
#include "alchemy.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace rpg {

struct Result {
    bool success = false;
    bool has_output = false;
    bool output_is_item = false;
    bool anomaly = false;
    std::uint64_t output = 0;
    std::string message;
};

// Один алгоритм скрытых правил для данного world seed.
// Он намеренно НЕ выводится командами игры; для проверки используется модульный тест.
struct HiddenCondition {
    std::string facing;
    int jumps = 0;
    int prayer_days = 0;
    int rare_roll_threshold = 5;  // 0,05% при соблюдении всех условий.
};
inline std::uint64_t splitmix64(std::uint64_t& seed) {
    std::uint64_t x = (seed += 0x9e3779b97f4a7c15ULL);
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
inline HiddenCondition hidden_condition(std::uint64_t world_seed) {
    if (!world_seed) throw std::runtime_error("World seed must be positive");
    std::uint64_t x = world_seed;
    const char* facing[] = {"north", "south", "east", "west"};
    HiddenCondition result;
    result.facing = facing[splitmix64(x) % 4];
    result.jumps = 2 + static_cast<int>(splitmix64(x) % 5);
    result.prayer_days = 3 + static_cast<int>(splitmix64(x) % 5);
    return result;
}

inline bool is_reserved(const GameState& s, bool item, std::uint64_t id, std::uint64_t except_order = 0) {
    for (const auto& o : s.orders) {
        if (o.status != OrderStatus::Open || o.id == except_order) continue;
        if ((o.input1 == id && order_uses_item(o.operation, true) == item) ||
            (o.input2 == id && order_uses_item(o.operation, false) == item)) return true;
    }
    return false;
}

inline Profession required_profession(Operation op) {
    switch (op) {
    case Operation::Extract: case Operation::Refine: case Operation::Mix: return Profession::Alchemist;
    case Operation::Forge: case Operation::Assemble: return Profession::Blacksmith;
    case Operation::Carve: return Profession::Carpenter;
    case Operation::Decorate: return Profession::Jeweler;
    case Operation::Brew: return Profession::Herbalist;
    case Operation::Cook: return Profession::Cook;
    case Operation::Weave: return Profession::Tailor;
    case Operation::Engineer: return Profession::Engineer;
    case Operation::Attune: return Profession::Mystic;
    }
    throw std::runtime_error("Invalid operation");
}
inline int operation_difficulty(Operation op) {
    switch (op) {
    case Operation::Extract: case Operation::Carve: case Operation::Cook: return 1;
    case Operation::Refine: case Operation::Forge: case Operation::Brew: case Operation::Weave: return 2;
    case Operation::Assemble: case Operation::Decorate: case Operation::Engineer: return 3;
    case Operation::Attune: return 5;
    case Operation::Mix: return 0;
    }
    return 1;
}
inline int craft_success_chance(int skill_level, Operation op) {
    return std::clamp(8000 + skill_level * 130 - operation_difficulty(op) * 220, 4000, 9900);
}

class Engine {
public:
    Engine(GameState& state, const ElementList& world)
        : state_(state), world_(world), rng_(std::random_device{}()) {}
    Engine(GameState& state, const ElementList& world, std::uint32_t test_seed)
        : state_(state), world_(world), rng_(test_seed) {}

    // Все добытчики берут сырьё из конечных узлов мира.
    Result gather(std::uint64_t actor_id, std::uint64_t node_id) {
        (void)state_.actor(actor_id);
        const ResourceNode n = state_.node(node_id);
        if (n.remaining <= 0) throw std::runtime_error("Resource deposit is exhausted");
        Profession p = Profession::Miner;
        MaterialKind material = MaterialKind::Ore;
        switch (n.kind) {
        case NodeKind::OreVein: p = Profession::Miner; material = MaterialKind::Ore; break;
        case NodeKind::GemVein: p = Profession::Miner; material = MaterialKind::Gem; break;
        case NodeKind::Forest: p = Profession::Forester; material = MaterialKind::Wood; break;
        case NodeKind::HerbPatch: p = Profession::Herbalist; material = MaterialKind::Herb; break;
        case NodeKind::FiberPatch: p = Profession::Forester; material = MaterialKind::Fiber; break;
        case NodeKind::PrimalSpring: p = Profession::Alchemist; material = MaterialKind::PrimalOre; break;
        }
        const int quality = clamp100(40 + state_.level(actor_id, p) * 3 + roll_between(-10, 10));
        state_.node(node_id).remaining -= 1;
        const auto id = new_lot(actor_id, material, 1, n.element, "-", std::max(1, quality), 0, 0);
        state_.gain_xp(actor_id, p, 20);
        event(actor_id, "gather", node_id, 0, id, true);
        return {true, true, false, false, id, "Gathered one " + name(material)};
    }

    // Передача материалов: часть стека отделяется с новым ID и ссылкой на источник.
    std::uint64_t give_lot(std::uint64_t from, std::uint64_t to, std::uint64_t id, int qty = 1) {
        (void)state_.actor(from); (void)state_.actor(to);
        if (from == to) throw std::runtime_error("Actors must differ");
        if (is_reserved(state_, false, id)) throw std::runtime_error("Material reserved by an open order");
        const Lot x = require_lot(from, id, qty);
        std::uint64_t moved_id = id;
        if (x.quantity == qty) {
            state_.lot(id).owner = to;
        } else {
            state_.lot(id).quantity -= qty;
            moved_id = new_lot(to, x.kind, qty, x.element, x.form, x.quality, x.id, 0);
        }
        event(from, "give-lot", id, to, moved_id, true);
        return moved_id;
    }
    void give_item(std::uint64_t from, std::uint64_t to, std::uint64_t id) {
        (void)state_.actor(to);
        if (from == to) throw std::runtime_error("Actors must differ");
        if (is_reserved(state_, true, id)) throw std::runtime_error("Item reserved by an open order");
        (void)require_item(from, id);
        state_.item(id).owner = to;
        event(from, "give-item", id, to, id, true);
    }

    // Важное правило: первая валидация НЕ меняет состояние мира.
    Result perform(std::uint64_t actor_id, Operation op, std::uint64_t input1,
                   std::uint64_t input2 = 0, int test_roll = -1,
                   int test_secret_roll = -1, std::uint64_t executing_order = 0) {
        (void)state_.actor(actor_id);
        const auto prof = required_profession(op);
        const int skill_level = state_.level(actor_id, prof);
        const auto inputs = checked_inputs(actor_id, op, input1, input2, executing_order);
        const auto& a = inputs.a;
        const auto& b = inputs.b;
        const auto& ia = inputs.ia;
        const auto& ib = inputs.ib;
        const bool first_is_item = order_uses_item(op, true);
        const bool second_is_item = order_uses_item(op, false);

        const int roll = test_roll >= 0 ? checked_roll(test_roll) : roll_between(0, 9999);
        Result result;
        result.message = "Craft failed; materials may be lost";
        if (op == Operation::Mix) {
            auto odds = alchemy::chances(world_, a.element, b.element);
            // Навык уменьшает долю неудач, но не отменяет редкость высоких элементов.
            // Уровень 1 сохраняет точные вероятности v2.
            const int bonus = odds.nothing * std::min(50, std::max(0, skill_level - 1)) / 200;
            const int old_total = odds.element + odds.variant;
            if (old_total > 0 && bonus > 0) {
                const int element_bonus = bonus * odds.element / old_total;
                odds.element += element_bonus;
                odds.variant += bonus - element_bonus;
                odds.nothing -= bonus;
            }
            if (roll < odds.element || roll < odds.element + odds.variant) {
                const bool variant = roll >= odds.element;
                const auto element_id = variant ? odds.target_id - 1 : odds.target_id;
                const char* forms[] = {"crystal", "porous", "amorphous"};
                const std::string form = variant ? forms[roll_between(0, 2)] : "-";
                const Element* elem = alchemy::find_element(world_, element_id);
                const int quality = elem ? clamp100(35 + elem->hardness * 4 + skill_level) : 1;
                result = make_lot(actor_id, MaterialKind::Element, element_id, form,
                                  std::max(1, quality), input1, input2, "Alchemy produced element");
            }
        } else if (roll < craft_success_chance(skill_level, op)) {
            switch (op) {
            case Operation::Extract:
                result = make_lot(actor_id, MaterialKind::Element, a.element, "-",
                                  clamp_quality(a.quality + 5), input1, 0, "Extracted element"); break;
            case Operation::Refine: {
                const Element* e = alchemy::find_element(world_, a.element);
                const int score = e ? e->hardness * 4 + e->conductivity / 2 : 0;
                result = make_lot(actor_id, MaterialKind::Metal, a.element, "-",
                                  clamp_quality((a.quality + score) / 2 + skill_level / 2),
                                  input1, 0, "Refined metal"); break;
            }
            case Operation::Forge:
                result = make_item(actor_id, ItemKind::Blade, input1, 0,
                                   clamp_quality(a.quality + skill_level / 2),
                                   clamp100(a.quality / 2 + skill_level), 0, 0, "Forged blade"); break;
            case Operation::Carve:
                result = make_item(actor_id, ItemKind::Handle, input1, 0,
                                   clamp_quality(a.quality + skill_level / 2),
                                   0, clamp100(a.quality + skill_level), 0, "Carved handle"); break;
            case Operation::Assemble:
                result = make_item(actor_id, ItemKind::Sword, input1, input2,
                                   clamp_quality((ia.quality + ib.quality) / 2 + skill_level / 3),
                                   ia.hardness, ib.comfort, clamp100(ia.resonance + ib.resonance), "Assembled sword");
                state_.item(result.output).anomaly = ia.anomaly || ib.anomaly;
                break;
            case Operation::Decorate:
                state_.item(input1).decorated = true;
                state_.item(input1).decoration_lot = input2;
                state_.item(input1).resonance = clamp100(ia.resonance + b.quality / 3 + skill_level / 2);
                result = {true, true, true, false, input1, "Decorated the existing sword"}; break;
            case Operation::Brew:
                result = make_lot(actor_id, MaterialKind::Potion, 0, "-",
                                  clamp_quality(a.quality + skill_level), input1, 0, "Brewed potion"); break;
            case Operation::Cook:
                result = make_lot(actor_id, MaterialKind::Food, 0, "-",
                                  clamp_quality(a.quality + skill_level), input1, 0, "Cooked food"); break;
            case Operation::Weave:
                result = make_lot(actor_id, MaterialKind::Cloth, 0, "-",
                                  clamp_quality(a.quality + skill_level), input1, 0, "Wove cloth"); break;
            case Operation::Engineer:
                result = make_item(actor_id, ItemKind::Device, input1, input2,
                                   clamp_quality((a.quality + b.quality) / 2 + skill_level),
                                   clamp100(a.quality / 2), clamp100(b.quality / 2), 0,
                                   "Built device"); break;
            case Operation::Attune:
                state_.item(input1).attuned = true;
                state_.item(input1).attunement_lot = input2;
                state_.item(input1).resonance = clamp100(ia.resonance + b.quality / 2 + skill_level);
                result = {true, true, true, false, input1, "Attuned the existing sword"}; break;
            case Operation::Mix: break;
            }
        }

        // Действительно расходуем ровно те материалы, которые участвовали в операции.
        // Если операция не удалась, входные предметы сохраняются, а материалы теряются.
        if (op != Operation::Assemble) {
            if (!first_is_item) consume_lot(input1);
            if (input2 && !second_is_item) consume_lot(input2);
        } else if (result.success) {
            state_.item(input1).owner = 0;
            state_.item(input2).owner = 0;
        }
        if (op == Operation::Assemble && !result.success) {
            // Сборка неудачна, но клинок и рукоять остаются у исполнителя.
            result.message = "Assembly failed; components remain usable";
        }
        state_.gain_xp(actor_id, prof, 20 + static_cast<std::uint64_t>(operation_difficulty(op) * 5));
        if (result.success && result.output_is_item &&
            (op == Operation::Forge || op == Operation::Carve)) {
            const int anomaly_roll = test_secret_roll >= 0
                ? checked_roll(test_secret_roll) : roll_between(0, 9999);
            const auto secret = hidden_condition(state_.seed);
            const Actor astate = state_.actor(actor_id);
            if (astate.facing == secret.facing && astate.jumps >= secret.jumps &&
                astate.consecutive_prayers >= secret.prayer_days &&
                astate.last_prayer_day == state_.day &&
                anomaly_roll < (knows(actor_id, op == Operation::Forge ? "structural-memory" : "harmonic-wood")
                    ? secret.rare_roll_threshold * 5 : secret.rare_roll_threshold)) {
                state_.item(result.output).anomaly = true;
                state_.item(result.output).resonance = 30;
                state_.discoveries.push_back({state_.allocate(), actor_id, result.output,
                    op == Operation::Forge ? "structural-memory" : "harmonic-wood", state_.day});
                result.anomaly = true;
                result.message += "; extraordinary material state discovered";
            }
        }
        // После каждой попытки изготовления поведенческая серия прыжков сбрасывается.
        state_.actor(actor_id).jumps = 0;
        event(actor_id, name(op), input1, input2, result.output, result.success);
        return result;
    }

    // Заказ: принимаем реальные ресурсы заказчика и резервируем за NPC.
    std::uint64_t create_order(std::uint64_t customer, std::uint64_t worker, Operation op,
                               std::uint64_t input1, std::uint64_t input2 = 0) {
        if (customer == worker || !state_.actor(worker).npc)
            throw std::runtime_error("Worker must be a different NPC");
        (void)state_.actor(customer);
        (void)checked_inputs(customer, op, input1, input2);
        // Транзакция в памяти: если хотя бы один ресурс отсутствует, ничего не переносим.
        GameState previous = state_;
        try {
            auto reserve = [&](std::uint64_t id, bool is_item) -> std::uint64_t {
                if (is_item) { give_item(customer, worker, id); return id; }
                return give_lot(customer, worker, id, 1);
            };
            const auto a = reserve(input1, order_uses_item(op, true));
            const auto b = input2 ? reserve(input2, order_uses_item(op, false)) : 0;
            const auto id = state_.allocate();
            state_.orders.push_back({id, customer, worker, op, a, b, OrderStatus::Open, 0, false});
            event(customer, "order", a, b, id, true);
            return id;
        } catch (...) { state_ = std::move(previous); throw; }
    }

    Result fulfill_order(std::uint64_t id, int test_roll = -1, int test_secret_roll = -1) {
        const WorkOrder o = state_.order(id);
        if (o.status != OrderStatus::Open) throw std::runtime_error("Order is already closed");
        GameState previous = state_;
        try {
            Result result = perform(o.worker, o.operation, o.input1, o.input2,
                                    test_roll, test_secret_roll, id);
            if (result.success) {
                if (result.output_is_item) {
                    // Возвращаем результат заказчику.
                    state_.item(result.output).owner = o.customer;
                } else state_.lot(result.output).owner = o.customer;
            } else {
                // Незатронутые компоненты возвращаются заказчику при неудаче.
                for (auto input : {o.input1, o.input2}) if (input) {
                    if (order_uses_item(o.operation, input == o.input1)) {
                        if (state_.item(input).owner == o.worker) state_.item(input).owner = o.customer;
                    } else {
                        if (state_.lot(input).owner == o.worker && state_.lot(input).quantity > 0)
                            state_.lot(input).owner = o.customer;
                    }
                }
            }
            auto& order = state_.order(id);
            order.status = result.success ? OrderStatus::Fulfilled : OrderStatus::Failed;
            order.output = result.output;
            order.output_is_item = result.output_is_item;
            event(o.worker, "fulfill", id, 0, result.output, result.success);
            return result;
        } catch (...) { state_ = std::move(previous); throw; }
    }

    // Отмена незапущенного заказа возвращает все зарезервированные материалы.
    void cancel_order(std::uint64_t customer, std::uint64_t id) {
        const WorkOrder o = state_.order(id);
        if (o.status != OrderStatus::Open || o.customer != customer)
            throw std::runtime_error("Only the customer may cancel an open order");
        for (auto input : {o.input1, o.input2}) if (input) {
            if (order_uses_item(o.operation, input == o.input1))
                state_.item(input).owner = customer;
            else state_.lot(input).owner = customer;
        }
        state_.order(id).status = OrderStatus::Cancelled;
        event(customer, "cancel-order", id, 0, 0, true);
    }

    // Исследование необычного свойства не создаёт новый предмет.
    // Знание привязывается к конкретному персонажу, а не к глобальному уровню.
    bool research(std::uint64_t actor_id, std::uint64_t item_id, Profession p,
                  int test_roll = -1) {
        (void)state_.actor(actor_id);
        if (state_.item(item_id).owner != actor_id)
            throw std::runtime_error("Researcher must possess the investigated item");
        const auto effect = effect_for_item(item_id);
        const int level = state_.level(actor_id, p);
        const int chance = std::clamp(2500 + level * 300, 1000, 9500);
        const int roll = test_roll >= 0 ? checked_roll(test_roll) : roll_between(0, 9999);
        const bool found = !effect.empty() && roll < chance;
        state_.gain_xp(actor_id, p, 15);
        if (found) add_knowledge(actor_id, effect);
        event(actor_id, "research-" + name(p), item_id, 0, 0, found);
        return found;
    }

    void teach(std::uint64_t from, std::uint64_t to, const std::string& effect) {
        (void)state_.actor(from); (void)state_.actor(to);
        if (from == to || !knows(from, effect))
            throw std::runtime_error("Teacher does not know this effect");
        add_knowledge(to, effect);
        event(from, "teach-" + effect, to, 0, 0, true);
    }

    void face(std::uint64_t actor_id, const std::string& direction) {
        if (direction != "north" && direction != "south" && direction != "west" && direction != "east")
            throw std::runtime_error("Direction: north/south/east/west");
        state_.actor(actor_id).facing = direction;
        event(actor_id, "face-" + direction, 0, 0, 0, true);
    }
    void jump(std::uint64_t actor_id, int count) {
        if (count < 1 || count > 100 || count + state_.actor(actor_id).jumps > 100)
            throw std::runtime_error("Jump count must stay in 1..100");
        state_.actor(actor_id).jumps += count;
        event(actor_id, "jump", static_cast<std::uint64_t>(count), 0, 0, true);
    }
    void pray(std::uint64_t actor_id) {
        Actor& a = state_.actor(actor_id);
        if (a.last_prayer_day == state_.day) throw std::runtime_error("Already prayed today");
        a.consecutive_prayers = a.last_prayer_day + 1 == state_.day ?
            a.consecutive_prayers + 1 : 1;
        a.last_prayer_day = state_.day;
        event(actor_id, "pray", 0, 0, 0, true);
    }
    void advance_days(int count) {
        if (count < 1 || count > 10000 || state_.day > std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(count))
            throw std::runtime_error("Days must be in 1..10000");
        state_.day += static_cast<std::uint64_t>(count);
    }

private:
    struct CheckedInputs {
        Lot a, b;
        Item ia, ib;
    };
    CheckedInputs checked_inputs(std::uint64_t actor_id, Operation op,
                                 std::uint64_t input1, std::uint64_t input2,
                                 std::uint64_t executing_order = 0) const {
        const bool first_is_item = order_uses_item(op, true);
        const bool second_is_item = order_uses_item(op, false);
        const bool needs_second = (op == Operation::Mix || op == Operation::Assemble ||
            op == Operation::Engineer || op == Operation::Decorate || op == Operation::Attune);
        if (!input1 || (needs_second != (input2 != 0)))
            throw std::runtime_error("Wrong number of inputs for operation " + name(op));
        if (input1 == input2 && first_is_item != second_is_item)
            throw std::runtime_error("A material and item cannot share the same ID");
        if (input1 == input2 && first_is_item)
            throw std::runtime_error("Cannot use one item as two components");
        if (is_reserved(state_, first_is_item, input1, executing_order) ||
            (input2 && is_reserved(state_, second_is_item, input2, executing_order)))
            throw std::runtime_error("Input is reserved by another work order");

        CheckedInputs inputs;
        auto& a = inputs.a;
        auto& b = inputs.b;
        auto& ia = inputs.ia;
        auto& ib = inputs.ib;
        if (first_is_item) ia = require_item(actor_id, input1);
        else a = require_lot(actor_id, input1, input1 == input2 ? 2 : 1);
        if (input2) {
            if (second_is_item) ib = require_item(actor_id, input2);
            else b = require_lot(actor_id, input2, input1 == input2 ? 2 : 1);
        }
        // Проверяем совместимость по КАТЕГОРИЯМ, а не по жёсткому ID элемента.
        switch (op) {
        case Operation::Extract:
            need(a.kind == MaterialKind::PrimalOre, "Extraction needs primal source material"); break;
        case Operation::Refine:
            need(a.kind == MaterialKind::Ore, "Metal refining needs raw ore"); break;
        case Operation::Mix:
            need(a.kind == MaterialKind::Element && b.kind == MaterialKind::Element,
                 "Alchemy needs two element lots");
            (void)alchemy::chances(world_, a.element, b.element);
            break;
        case Operation::Forge: need(a.kind == MaterialKind::Metal, "Forge needs metal"); break;
        case Operation::Carve: need(a.kind == MaterialKind::Wood, "Carve needs wood"); break;
        case Operation::Assemble:
            need(ia.kind == ItemKind::Blade && ib.kind == ItemKind::Handle,
                 "A sword needs one real blade and one real handle"); break;
        case Operation::Decorate:
            need(ia.kind == ItemKind::Sword && !ia.decorated && b.kind == MaterialKind::Gem,
                 "Decoration needs an undecorated sword and a gem"); break;
        case Operation::Brew: case Operation::Cook:
            need(a.kind == MaterialKind::Herb, "Needs herbs"); break;
        case Operation::Weave: need(a.kind == MaterialKind::Fiber, "Needs fibers"); break;
        case Operation::Engineer:
            need(a.kind == MaterialKind::Metal && b.kind == MaterialKind::Wood,
                 "A mechanism needs metal and wood"); break;
        case Operation::Attune:
            need(ia.kind == ItemKind::Sword && !ia.attuned && b.kind == MaterialKind::Gem,
                 "Attunement needs a sword and a gem"); break;
        }

        return inputs;
    }
    bool knows(std::uint64_t actor_id, const std::string& effect) const {
        return std::any_of(state_.knowledge.begin(), state_.knowledge.end(),
            [&](const Knowledge& k){ return k.actor == actor_id && k.effect == effect; });
    }
    void add_knowledge(std::uint64_t actor_id, const std::string& effect) {
        for (auto& k : state_.knowledge) if (k.actor == actor_id && k.effect == effect) {
            k.observations = std::min(10000, k.observations + 1);
            return;
        }
        state_.knowledge.push_back({actor_id, effect, 1});
    }
    std::string effect_for_item(std::uint64_t id) const {
        for (const auto& d : state_.discoveries) if (d.item == id) return d.effect;
        const auto& i = state_.item(id);
        if (i.kind == ItemKind::Sword) {
            for (auto parent : {i.component1, i.component2}) {
                if (parent) {
                    const auto effect = effect_for_item(parent);
                    if (!effect.empty()) return effect;
                }
            }
        }
        return {};
    }
    static void need(bool condition, const char* msg) {
        if (!condition) throw std::runtime_error(msg);
    }
    static int checked_roll(int n) {
        if (n < 0 || n > 9999) throw std::runtime_error("Roll must be 0..9999");
        return n;
    }
    static int clamp_quality(int n) { return std::clamp(n, 1, 100); }
    int roll_between(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng_); }
    Lot require_lot(std::uint64_t owner, std::uint64_t id, int quantity = 1) const {
        const auto& x = state_.lot(id);
        if (x.owner != owner || x.quantity < quantity || quantity <= 0)
            throw std::runtime_error("Required material is not owned or has insufficient quantity");
        return x;
    }
    Item require_item(std::uint64_t owner, std::uint64_t id) const {
        const auto& x = state_.item(id);
        if (x.owner != owner) throw std::runtime_error("Required item is absent or not owned");
        return x;
    }
    void consume_lot(std::uint64_t id) {
        auto& x = state_.lot(id);
        if (x.quantity <= 0) throw std::runtime_error("Material already consumed");
        if (--x.quantity == 0) x.owner = 0;
    }
    std::uint64_t new_lot(std::uint64_t owner, MaterialKind kind, int qty,
                          std::uint64_t elem, const std::string& form, int quality,
                          std::uint64_t source1, std::uint64_t source2) {
        const auto id = state_.allocate();
        state_.lots.push_back({id, owner, kind, qty, elem, form, quality, source1, source2});
        return id;
    }
    Result make_lot(std::uint64_t owner, MaterialKind kind, std::uint64_t elem,
                    const std::string& form, int quality, std::uint64_t source1,
                    std::uint64_t source2, const std::string& explanation) {
        const auto id = new_lot(owner, kind, 1, elem, form, quality, source1, source2);
        return {true, true, false, false, id, explanation};
    }
    Result make_item(std::uint64_t owner, ItemKind kind, std::uint64_t c1, std::uint64_t c2,
                     int quality, int hardness, int comfort, int resonance,
                     const std::string& explanation) {
        const auto id = state_.allocate();
        state_.items.push_back({id, owner, kind, c1, c2, 0, 0, quality, hardness,
                                comfort, resonance, false, false, false});
        return {true, true, true, false, id, explanation};
    }
    void event(std::uint64_t actor_id, const std::string& verb, std::uint64_t a,
               std::uint64_t b, std::uint64_t output, bool success) {
        state_.events.push_back({state_.allocate(), actor_id, verb, a, b, output,
                                 success, state_.day});
    }
    GameState& state_;
    const ElementList& world_;
    std::mt19937 rng_;
};

} // namespace rpg
