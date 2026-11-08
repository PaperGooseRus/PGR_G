#pragma once

#include "elements.hpp"
#include "storage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Модель игрового мира. Здесь нет обработки команд или правил крафта.
// Вся изменяемая экономика сохраняется ОДНИМ снимком world_state.md.
namespace rpg {

enum class Profession {
    Alchemist, Blacksmith, Carpenter, Jeweler, Forester, Miner,
    Herbalist, Cook, Tailor, Engineer, Mystic
};
enum class MaterialKind { Ore, PrimalOre, Element, Metal, Wood, Gem, Herb, Fiber, Cloth, Food, Potion };
enum class ItemKind { Blade, Handle, Sword, Device };
enum class NodeKind { OreVein, Forest, GemVein, HerbPatch, FiberPatch, PrimalSpring };
enum class Operation { Extract, Refine, Mix, Forge, Carve, Assemble, Decorate, Brew, Cook, Weave, Engineer, Attune };
enum class OrderStatus { Open, Fulfilled, Failed, Cancelled };

inline constexpr std::array<const char*, 11> profession_names = {
    "alchemist", "blacksmith", "carpenter", "jeweler", "forester", "miner",
    "herbalist", "cook", "tailor", "engineer", "mystic"
};
inline constexpr std::array<const char*, 11> material_names = {
    "ore", "primal-ore", "element", "metal", "wood", "gem", "herb", "fiber", "cloth", "food", "potion"
};
inline constexpr std::array<const char*, 4> item_names = {"blade", "handle", "sword", "device"};
inline constexpr std::array<const char*, 6> node_names = {"ore-vein", "forest", "gem-vein", "herb-patch", "fiber-patch", "primal-spring"};
inline constexpr std::array<const char*, 12> operation_names = {
    "extract", "refine", "mix", "forge", "carve", "assemble", "decorate",
    "brew", "cook", "weave", "engineer", "attune"
};
inline constexpr std::array<const char*, 4> status_names = {"open", "fulfilled", "failed", "cancelled"};

// Только значения из перечислений, а не произвольный пользовательский текст.
template<typename Enum, std::size_t N>
inline std::string label(Enum value, const std::array<const char*, N>& names) {
    const auto i = static_cast<std::size_t>(value);
    if (i >= N) throw std::runtime_error("Invalid enum value");
    return names[i];
}
template<typename Enum, std::size_t N>
inline Enum parse_enum(const std::string& s, const std::array<const char*, N>& names) {
    for (std::size_t i = 0; i < N; ++i) if (s == names[i]) return static_cast<Enum>(i);
    throw std::runtime_error("Unknown enum value: " + s);
}
inline std::string name(Profession v) { return label(v, profession_names); }
inline std::string name(MaterialKind v) { return label(v, material_names); }
inline std::string name(ItemKind v) { return label(v, item_names); }
inline std::string name(NodeKind v) { return label(v, node_names); }
inline std::string name(Operation v) { return label(v, operation_names); }
inline std::string name(OrderStatus v) { return label(v, status_names); }
inline Profession profession(const std::string& s) { return parse_enum<Profession>(s, profession_names); }
inline MaterialKind material_kind(const std::string& s) { return parse_enum<MaterialKind>(s, material_names); }
inline ItemKind item_kind(const std::string& s) { return parse_enum<ItemKind>(s, item_names); }
inline NodeKind node_kind(const std::string& s) { return parse_enum<NodeKind>(s, node_names); }
inline Operation operation(const std::string& s) { return parse_enum<Operation>(s, operation_names); }
inline OrderStatus order_status(const std::string& s) { return parse_enum<OrderStatus>(s, status_names); }

inline int clamp100(int n) { return std::max(0, std::min(100, n)); }
inline std::uint64_t experience_for_level(int level) {
    if (level < 1 || level > 100) throw std::runtime_error("Level must be 1..100");
    return static_cast<std::uint64_t>(20 * (level - 1) * (level - 1));
}
inline int level_from_xp(std::uint64_t experience) {
    int level = 1;
    while (level < 100 && experience >= experience_for_level(level + 1)) ++level;
    return level;
}

struct Actor {
    std::uint64_t id = 0;
    std::string name;
    bool npc = false;
    std::string facing = "north";
    int jumps = 0;
    int consecutive_prayers = 0;
    std::uint64_t last_prayer_day = 0;
};
struct Skill {
    std::uint64_t actor = 0;
    Profession profession = Profession::Alchemist;
    std::uint64_t xp = 0;
};
struct Lot {
    std::uint64_t id = 0;
    std::uint64_t owner = 0;   // 0: израсходован, остаётся для истории происхождения
    MaterialKind kind = MaterialKind::Ore;
    int quantity = 0;
    std::uint64_t element = 0; // ID элемента, если физический состав известен
    std::string form = "-";
    int quality = 0;          // 1..100
    std::uint64_t source1 = 0; // ID предыдущих лотов (0: природный источник)
    std::uint64_t source2 = 0;
};
struct Item {
    std::uint64_t id = 0;
    std::uint64_t owner = 0;      // 0: встроен в другой предмет
    ItemKind kind = ItemKind::Blade;
    std::uint64_t component1 = 0; // лот для деталей; предмет для сборки
    std::uint64_t component2 = 0;
    std::uint64_t decoration_lot = 0;
    std::uint64_t attunement_lot = 0;
    int quality = 0;
    int hardness = 0;
    int comfort = 0;
    int resonance = 0;
    bool decorated = false;
    bool attuned = false;
    bool anomaly = false;
};
struct ResourceNode {
    std::uint64_t id = 0;
    NodeKind kind = NodeKind::OreVein;
    int remaining = 0;
    std::uint64_t element = 0;
};
struct WorkOrder {
    std::uint64_t id = 0;
    std::uint64_t customer = 0;
    std::uint64_t worker = 0;
    Operation operation = Operation::Forge;
    std::uint64_t input1 = 0;
    std::uint64_t input2 = 0;
    OrderStatus status = OrderStatus::Open;
    std::uint64_t output = 0;
    bool output_is_item = false;
};
struct Discovery {
    std::uint64_t id = 0;
    std::uint64_t actor = 0;
    std::uint64_t item = 0;
    std::string effect = "-";
    std::uint64_t day = 0;
};
struct Knowledge {
    std::uint64_t actor = 0;
    std::string effect;
    int observations = 0;
};
struct GameEvent {
    std::uint64_t id = 0;
    std::uint64_t actor = 0;
    std::string verb;
    std::uint64_t input1 = 0;
    std::uint64_t input2 = 0;
    std::uint64_t output = 0;
    bool success = false;
    std::uint64_t day = 0;
};

struct GameState {
    std::uint64_t seed = 0;
    std::uint64_t day = 1;
    std::uint64_t next_id = 100;
    std::vector<Actor> actors;
    std::vector<Skill> skills;
    std::vector<Lot> lots;
    std::vector<Item> items;
    std::vector<ResourceNode> nodes;
    std::vector<WorkOrder> orders;
    std::vector<Discovery> discoveries;
    std::vector<Knowledge> knowledge;
    std::vector<GameEvent> events;

    std::uint64_t allocate() {
        if (next_id == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Game ID space exhausted");
        return next_id++;
    }
    Actor& actor(std::uint64_t id) {
        for (auto& a : actors) if (a.id == id) return a;
        throw std::runtime_error("Unknown actor ID: " + std::to_string(id));
    }
    const Actor& actor(std::uint64_t id) const {
        for (const auto& a : actors) if (a.id == id) return a;
        throw std::runtime_error("Unknown actor ID: " + std::to_string(id));
    }
    Lot& lot(std::uint64_t id) {
        for (auto& x : lots) if (x.id == id) return x;
        throw std::runtime_error("Unknown material lot ID: " + std::to_string(id));
    }
    const Lot& lot(std::uint64_t id) const {
        for (const auto& x : lots) if (x.id == id) return x;
        throw std::runtime_error("Unknown material lot ID: " + std::to_string(id));
    }
    Item& item(std::uint64_t id) {
        for (auto& x : items) if (x.id == id) return x;
        throw std::runtime_error("Unknown item ID: " + std::to_string(id));
    }
    const Item& item(std::uint64_t id) const {
        for (const auto& x : items) if (x.id == id) return x;
        throw std::runtime_error("Unknown item ID: " + std::to_string(id));
    }
    ResourceNode& node(std::uint64_t id) {
        for (auto& x : nodes) if (x.id == id) return x;
        throw std::runtime_error("Unknown resource node: " + std::to_string(id));
    }
    WorkOrder& order(std::uint64_t id) {
        for (auto& x : orders) if (x.id == id) return x;
        throw std::runtime_error("Unknown order: " + std::to_string(id));
    }
    int level(std::uint64_t actor_id, Profession p) const {
        actor(actor_id);
        for (const auto& s : skills) if (s.actor == actor_id && s.profession == p)
            return level_from_xp(s.xp);
        return 1;
    }
    void gain_xp(std::uint64_t actor_id, Profession p, std::uint64_t amount) {
        actor(actor_id);
        for (auto& s : skills) if (s.actor == actor_id && s.profession == p) {
            if (s.xp > std::numeric_limits<std::uint64_t>::max() - amount)
                throw std::runtime_error("XP overflow");
            s.xp += amount;
            return;
        }
        skills.push_back({actor_id, p, amount});
    }
};

// Для каждого нового мира первичные ресурсы существуют как конечные месторождения.
// Элемент руды выбирается по свойствам, а не по фиксированному номеру рецепта.
inline GameState new_game(const ElementList& world, std::uint64_t seed) {
    if (!world.head()) throw std::runtime_error("Elements must be loaded first");
    GameState state;
    state.seed = seed;
    std::uint64_t metal_id = 1;
    int best_score = std::numeric_limits<int>::min();
    std::uint64_t gem_id = 1;
    int best_gem = std::numeric_limits<int>::min();
    for (const Node* n = world.head(); n; n = n->next.get()) {
        const auto& e = n->data;
        const int score = e.conductivity + 5 * e.hardness;
        if (score > best_score) { metal_id = e.id; best_score = score; }
        const int gscore = e.hardness * 10 + e.conductivity;
        if (gscore > best_gem) { gem_id = e.id; best_gem = gscore; }
    }
    state.nodes = {
        {1, NodeKind::OreVein, 40, metal_id},
        {2, NodeKind::Forest, 40, 0},
        {3, NodeKind::GemVein, 12, gem_id},
        {4, NodeKind::HerbPatch, 24, 0},
        {5, NodeKind::FiberPatch, 24, 0},
        {6, NodeKind::PrimalSpring, 64, 1}
    };
    struct Starter { const char* n; bool npc; Profession p; int lvl; };
    const std::array<Starter, 12> starters = {{
        {"Player", false, Profession::Alchemist, 1},
        {"AlchemistNPC", true, Profession::Alchemist, 12},
        {"BlacksmithNPC", true, Profession::Blacksmith, 10},
        {"CarpenterNPC", true, Profession::Carpenter, 10},
        {"JewelerNPC", true, Profession::Jeweler, 12},
        {"ForesterNPC", true, Profession::Forester, 7},
        {"MinerNPC", true, Profession::Miner, 7},
        {"HerbalistNPC", true, Profession::Herbalist, 8},
        {"CookNPC", true, Profession::Cook, 8},
        {"TailorNPC", true, Profession::Tailor, 8},
        {"EngineerNPC", true, Profession::Engineer, 8},
        {"MysticNPC", true, Profession::Mystic, 8}
    }};
    for (std::size_t i = 0; i < starters.size(); ++i) {
        const auto& s = starters[i];
        const std::uint64_t id = i + 1;
        state.actors.push_back({id, s.n, s.npc, "north", 0, 0, 0});
        state.skills.push_back({id, s.p, experience_for_level(s.lvl)});
    }
    return state;
}

// Проверки ссылок нужны также парсеру файла.
inline bool alchemy_element_exists(const ElementList& world, std::uint64_t id) {
    for (const Node* n = world.head(); n; n = n->next.get())
        if (n->data.id == id) return true;
    return false;
}
inline bool saw_lot(const GameState& s, std::uint64_t id) {
    return std::any_of(s.lots.begin(), s.lots.end(), [=](const Lot& x){ return x.id == id; });
}
inline bool saw_item(const GameState& s, std::uint64_t id) {
    return std::any_of(s.items.begin(), s.items.end(), [=](const Item& x){ return x.id == id; });
}
inline bool order_uses_item(Operation op, bool first) {
    return op == Operation::Assemble || ((op == Operation::Decorate || op == Operation::Attune) && first);
}

inline void validate_state(const GameState& s, const ElementList& world) {
    if (!s.seed || !s.day || s.next_id < 100 || s.actors.empty())
        throw std::runtime_error("Invalid game header");
    std::set<std::uint64_t> actor_ids;
    for (const auto& a : s.actors) {
        if (!a.id || a.name.empty() || a.name.find('|') != std::string::npos ||
            (a.facing != "north" && a.facing != "south" && a.facing != "east" && a.facing != "west") ||
            a.jumps < 0 || a.jumps > 100 || a.consecutive_prayers < 0 ||
            a.last_prayer_day > s.day || !actor_ids.insert(a.id).second)
            throw std::runtime_error("Invalid actor record");
    }
    std::set<std::pair<std::uint64_t,int>> skill_keys;
    for (const auto& k : s.skills) {
        if (!actor_ids.count(k.actor) || !skill_keys.emplace(k.actor, static_cast<int>(k.profession)).second)
            throw std::runtime_error("Duplicate skill or invalid actor");
    }
    std::set<std::uint64_t> ids;
    for (const auto& l : s.lots) {
        if (l.id < 100 || l.id >= s.next_id || !ids.insert(l.id).second ||
            (l.owner && !actor_ids.count(l.owner)) || l.quantity < 0 ||
            (l.quantity == 0 && l.owner != 0) || (l.quantity > 0 && l.owner == 0) ||
            l.quality < 1 || l.quality > 100 ||
            (l.element != 0 && !alchemy_element_exists(world, l.element)) ||
            (l.form != "-" && l.form != "crystal" && l.form != "porous" && l.form != "amorphous"))
            throw std::runtime_error("Invalid material lot");
    }
    for (const auto& i : s.items) {
        if (i.id < 100 || i.id >= s.next_id || !ids.insert(i.id).second ||
            (i.owner && !actor_ids.count(i.owner)) || i.quality < 1 || i.quality > 100 ||
            i.hardness < 0 || i.hardness > 100 || i.comfort < 0 || i.comfort > 100 ||
            i.resonance < 0 || i.resonance > 100)
            throw std::runtime_error("Invalid item record");
    }
    std::set<std::uint64_t> node_ids;
    for (const auto& n : s.nodes) {
        if (!n.id || !node_ids.insert(n.id).second || n.remaining < 0 ||
            (n.element && !alchemy_element_exists(world, n.element)))
            throw std::runtime_error("Invalid resource node");
    }
    for (const auto& l : s.lots) {
        for (auto parent : {l.source1,l.source2}) if (parent) {
            const auto it = std::find_if(s.lots.begin(), s.lots.end(), [=](const Lot& x){ return x.id == parent; });
            if (it == s.lots.end() || parent >= l.id)
                throw std::runtime_error("Invalid material provenance link");
        }
    }
    std::map<std::uint64_t,int> embedded_count;
    for (const auto& i : s.items) {
        if (i.component1 == i.id || i.component2 == i.id)
            throw std::runtime_error("Cyclic item link");
        auto require_material = [&](std::uint64_t id, MaterialKind expected, bool older = true) {
            if (!id || !saw_lot(s, id) || s.lot(id).kind != expected || (older && id >= i.id))
                throw std::runtime_error("Wrong or missing source material for crafted item");
        };
        if (i.kind == ItemKind::Blade) {
            require_material(i.component1, MaterialKind::Metal);
            if (i.component2) throw std::runtime_error("Blade has an unexpected extra component");
        } else if (i.kind == ItemKind::Handle) {
            require_material(i.component1, MaterialKind::Wood);
            if (i.component2) throw std::runtime_error("Handle has an unexpected extra component");
        } else if (i.kind == ItemKind::Device) {
            require_material(i.component1, MaterialKind::Metal);
            require_material(i.component2, MaterialKind::Wood);
        } else if (i.kind == ItemKind::Sword) {
            if (!saw_item(s, i.component1) || !saw_item(s, i.component2) ||
                i.component1 == i.component2 || i.component1 >= i.id || i.component2 >= i.id ||
                s.item(i.component1).kind != ItemKind::Blade ||
                s.item(i.component2).kind != ItemKind::Handle ||
                s.item(i.component1).owner != 0 || s.item(i.component2).owner != 0)
                throw std::runtime_error("Sword must contain one consumed blade and handle");
            ++embedded_count[i.component1];
            ++embedded_count[i.component2];
        }
        if (i.decorated != (i.decoration_lot != 0) ||
            i.attuned != (i.attunement_lot != 0) ||
            ((i.decorated || i.attuned) && i.kind != ItemKind::Sword))
            throw std::runtime_error("Invalid item customization flags");
        if (i.decoration_lot) require_material(i.decoration_lot, MaterialKind::Gem, false);
        if (i.attunement_lot) require_material(i.attunement_lot, MaterialKind::Gem, false);
    }
    for (const auto& i : s.items) {
        if (i.kind != ItemKind::Blade && i.kind != ItemKind::Handle) {
            if (!i.owner) throw std::runtime_error("Composite item cannot be orphaned");
        } else if ((!i.owner && embedded_count[i.id] != 1) ||
                   (i.owner && embedded_count[i.id] != 0)) {
            throw std::runtime_error("Craft component has inconsistent ownership");
        }
    }
    for (const auto& o : s.orders) {
        if (o.id < 100 || o.id >= s.next_id || !ids.insert(o.id).second ||
            !actor_ids.count(o.customer) || !actor_ids.count(o.worker) || o.customer == o.worker)
            throw std::runtime_error("Invalid order record");
        if ((o.status == OrderStatus::Fulfilled && !o.output) ||
            (o.status != OrderStatus::Fulfilled && o.output) ||
            (o.output && (o.output_is_item ? !saw_item(s, o.output) : !saw_lot(s, o.output))))
            throw std::runtime_error("Order result is inconsistent");
    }
    for (const auto& d : s.discoveries) {
        if (d.id < 100 || d.id >= s.next_id || !ids.insert(d.id).second ||
            !actor_ids.count(d.actor) || !saw_item(s, d.item) || d.effect.empty() || d.day > s.day)
            throw std::runtime_error("Invalid discovery record");
    }
    std::set<std::pair<std::uint64_t,std::string>> knowledge_keys;
    for (const auto& k : s.knowledge) {
        if (!actor_ids.count(k.actor) || k.effect.empty() || k.effect.find('|') != std::string::npos ||
            k.observations < 1 || k.observations > 10000 ||
            !knowledge_keys.emplace(k.actor, k.effect).second)
            throw std::runtime_error("Invalid profession knowledge");
    }
    for (const auto& e : s.events) {
        if (e.id < 100 || e.id >= s.next_id || !ids.insert(e.id).second ||
            !actor_ids.count(e.actor) || e.verb.empty() || e.day > s.day)
            throw std::runtime_error("Invalid event record");
    }
    // Жёсткий контроль зарезервированных для заказов ресурсов.
    std::set<std::pair<bool,std::uint64_t>> reservations;
    for (const auto& o : s.orders) if (o.status == OrderStatus::Open) {
        for (auto input : {o.input1, o.input2}) if (input) {
            const bool item_input = order_uses_item(o.operation, input == o.input1);
            if (!reservations.emplace(item_input, input).second)
                throw std::runtime_error("One input reserved by multiple orders");
            if (item_input) {
                if (s.item(input).owner != o.worker) throw std::runtime_error("Reserved item moved away");
            } else {
                if (s.lot(input).owner != o.worker || s.lot(input).quantity < 1)
                    throw std::runtime_error("Reserved material was spent");
            }
        }
    }
}

// Forward declarations lifted below for validation helpers.

inline int boolean(const std::string& text) {
    if (text == "1") return 1;
    if (text == "0") return 0;
    throw std::runtime_error("Boolean must be 0 or 1");
}

inline void write_state(std::ostream& out, const GameState& s) {
    out << "# RPG shared crafting world\n\n<!-- RPG_STATE_V1 -->\n\n";
    out << "This is a single authoritative snapshot of all mutable game entities.\n\n";
    out << "## Meta\n\n| key | value |\n| --- | --- |\n";
    out << "| seed | " << s.seed << " |\n| day | " << s.day
        << " |\n| next_id | " << s.next_id << " |\n\n";
    out << "## Actors\n\n| id | name | npc | facing | jumps | prayers | prayed_day |\n"
        << "| --- | --- | --- | --- | --- | --- | --- |\n";
    for (const auto& a : s.actors) out << "| " << a.id << " | " << a.name << " | " << a.npc
        << " | " << a.facing << " | " << a.jumps << " | " << a.consecutive_prayers
        << " | " << a.last_prayer_day << " |\n";
    out << "\n## Skills\n\n| actor | profession | xp |\n| --- | --- | --- |\n";
    for (const auto& k : s.skills) out << "| " << k.actor << " | " << name(k.profession) << " | " << k.xp << " |\n";
    out << "\n## Lots\n\n| id | owner | kind | quantity | element | form | quality | source1 | source2 |\n"
        << "| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (const auto& l : s.lots) out << "| " << l.id << " | " << l.owner << " | " << name(l.kind)
        << " | " << l.quantity << " | " << l.element << " | " << l.form << " | " << l.quality
        << " | " << l.source1 << " | " << l.source2 << " |\n";
    out << "\n## Items\n\n| id | owner | kind | component1 | component2 | decoration_lot | attunement_lot | quality | hardness | comfort | resonance | decorated | attuned | anomaly |\n"
        << "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (const auto& i : s.items) out << "| " << i.id << " | " << i.owner << " | " << name(i.kind)
        << " | " << i.component1 << " | " << i.component2 << " | " << i.decoration_lot << " | " << i.attunement_lot
        << " | " << i.quality << " | " << i.hardness << " | " << i.comfort
        << " | " << i.resonance << " | " << i.decorated << " | " << i.attuned
        << " | " << i.anomaly << " |\n";
    out << "\n## Nodes\n\n| id | kind | remaining | element |\n| --- | --- | --- | --- |\n";
    for (const auto& n : s.nodes) out << "| " << n.id << " | " << name(n.kind)
        << " | " << n.remaining << " | " << n.element << " |\n";
    out << "\n## Orders\n\n| id | customer | worker | operation | input1 | input2 | status | output | output_item |\n"
        << "| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (const auto& o : s.orders) out << "| " << o.id << " | " << o.customer << " | " << o.worker
        << " | " << name(o.operation) << " | " << o.input1 << " | " << o.input2
        << " | " << name(o.status) << " | " << o.output << " | " << o.output_is_item << " |\n";
    out << "\n## Discoveries\n\n| id | actor | item | effect | day |\n| --- | --- | --- | --- | --- |\n";
    for (const auto& d : s.discoveries) out << "| " << d.id << " | " << d.actor << " | " << d.item
        << " | " << d.effect << " | " << d.day << " |\n";
    out << "\n## Knowledge\n\n| actor | effect | observations |\n| --- | --- | --- |\n";
    for (const auto& k : s.knowledge) out << "| " << k.actor << " | " << k.effect
        << " | " << k.observations << " |\n";
    out << "\n## Events\n\n| id | actor | verb | input1 | input2 | output | success | day |\n"
        << "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (const auto& e : s.events) out << "| " << e.id << " | " << e.actor << " | " << e.verb
        << " | " << e.input1 << " | " << e.input2 << " | " << e.output
        << " | " << e.success << " | " << e.day << " |\n";
}

inline void save_state(const fs::path& path, const GameState& state, const ElementList& world) {
    validate_state(state, world);
    md_store::save_atomically(path, [&](std::ostream& out) { write_state(out, state); });
}

inline GameState load_state(const fs::path& file, const ElementList& world) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read: " + file.string());
    GameState result;
    std::set<std::string> sections;
    std::string section, line;
    std::size_t line_number = 0;
    bool version_seen = false;
    std::set<std::string> meta;
    try {
        while (std::getline(in, line)) {
            ++line_number;
            line = trim(line);
            if (line == "<!-- RPG_STATE_V1 -->") { version_seen = true; continue; }
            if (line.rfind("## ", 0) == 0) {
                section = line.substr(3);
                if (!sections.insert(section).second) throw std::runtime_error("Repeated section");
                continue;
            }
            if (line.empty() || line[0] != '|') continue;
            const auto c = markdown_cells(line);
            if (c.empty() || c[0] == "---" || c[0] == "id" || c[0] == "actor" || c[0] == "key") continue;
            if (section == "Meta") {
                if (c.size() != 2 || !meta.insert(c[0]).second) throw std::runtime_error("Bad metadata");
                if (c[0] == "seed") result.seed = parse_u64(c[1]);
                else if (c[0] == "day") result.day = parse_u64(c[1]);
                else if (c[0] == "next_id") result.next_id = parse_u64(c[1]);
                else throw std::runtime_error("Unknown metadata");
            } else if (section == "Actors") {
                if (c.size() != 7) throw std::runtime_error("Bad actor row");
                result.actors.push_back({parse_u64(c[0]), c[1], boolean(c[2]) != 0,
                    c[3], parse_int(c[4]), parse_int(c[5]), parse_u64(c[6])});
            } else if (section == "Skills") {
                if (c.size() != 3) throw std::runtime_error("Bad skill row");
                result.skills.push_back({parse_u64(c[0]), profession(c[1]), parse_u64(c[2])});
            } else if (section == "Lots") {
                if (c.size() != 9) throw std::runtime_error("Bad lot row");
                result.lots.push_back({parse_u64(c[0]), parse_u64(c[1]), material_kind(c[2]),
                    parse_int(c[3]), parse_u64(c[4]), c[5], parse_int(c[6]),
                    parse_u64(c[7]), parse_u64(c[8])});
            } else if (section == "Items") {
                if (c.size() != 14) throw std::runtime_error("Bad item row");
                result.items.push_back({parse_u64(c[0]), parse_u64(c[1]), item_kind(c[2]),
                    parse_u64(c[3]), parse_u64(c[4]), parse_u64(c[5]), parse_u64(c[6]),
                    parse_int(c[7]), parse_int(c[8]), parse_int(c[9]), parse_int(c[10]),
                    boolean(c[11]) != 0, boolean(c[12]) != 0, boolean(c[13]) != 0});
            } else if (section == "Nodes") {
                if (c.size() != 4) throw std::runtime_error("Bad node row");
                result.nodes.push_back({parse_u64(c[0]), node_kind(c[1]), parse_int(c[2]), parse_u64(c[3])});
            } else if (section == "Orders") {
                if (c.size() != 9) throw std::runtime_error("Bad order row");
                result.orders.push_back({parse_u64(c[0]), parse_u64(c[1]), parse_u64(c[2]), operation(c[3]),
                    parse_u64(c[4]), parse_u64(c[5]), order_status(c[6]), parse_u64(c[7]), boolean(c[8]) != 0});
            } else if (section == "Discoveries") {
                if (c.size() != 5) throw std::runtime_error("Bad discovery row");
                result.discoveries.push_back({parse_u64(c[0]), parse_u64(c[1]), parse_u64(c[2]), c[3], parse_u64(c[4])});
            } else if (section == "Knowledge") {
                if (c.size() != 3) throw std::runtime_error("Bad knowledge row");
                result.knowledge.push_back({parse_u64(c[0]), c[1], parse_int(c[2])});
            } else if (section == "Events") {
                if (c.size() != 8) throw std::runtime_error("Bad event row");
                result.events.push_back({parse_u64(c[0]), parse_u64(c[1]), c[2], parse_u64(c[3]),
                    parse_u64(c[4]), parse_u64(c[5]), boolean(c[6]) != 0, parse_u64(c[7])});
            } else throw std::runtime_error("Unknown section or row");
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(file.string() + ": line " + std::to_string(line_number) + ": " + e.what());
    }
    if (!in.eof() && in.fail()) throw std::runtime_error("Error reading game state");
    for (const auto& required : {"Meta", "Actors", "Skills", "Lots", "Items", "Nodes", "Orders", "Discoveries", "Knowledge", "Events"})
        if (!sections.count(required)) throw std::runtime_error("Missing Markdown section: " + std::string(required));
    if (!version_seen || meta.size() != 3) throw std::runtime_error("Invalid state version or metadata");
    validate_state(result, world);
    return result;
}

} // namespace rpg
