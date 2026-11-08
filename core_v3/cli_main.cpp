#include "include/crafting.hpp"
#include "include/alchemy_history.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
constexpr const char* elements_filename = "elements.md";
constexpr const char* state_filename = "world_state.md";
constexpr const char* legacy_history_filename = "alchemy.md";

void help() {
    std::cout << R"(RPG Crafting Core v3 (C++17)
No arguments: summary. --play: interactive console. Commands:
  help                         Show these commands
  elements                     Show primary elements
  odds A B                     Show original alchemy probabilities
  history                      Show legacy alchemy.md and crafting events
  actors                       Show player and NPC professions/levels
  nodes                        Show finite natural resources
  inventory ACTOR              Show owned material lots and items
  inspect ITEM                 Show item structure and its component IDs
  orders                       Show NPC work orders
  discoveries                  Show unusual discoveries (not the hidden conditions)
  knowledge                    Show learned effects for each character
  research ACTOR ITEM PROF     Study an owned unusual item; gain professional XP
  teach FROM TO EFFECT         Share a previously studied effect
  gather ACTOR NODE            Extract one unit from a natural deposit
  craft ACTOR OP INPUT1 [INPUT2]  Process owned material(s)/item(s)
  mix A B                      Alias for craft 1 mix A B (consumes resources)
  give-lot FROM TO LOT [QTY]   Transfer actual material (stack split if needed)
  give-item FROM TO ITEM       Transfer actual item
  order CUSTOMER NPC OP INPUT1 [INPUT2]  Give inputs to NPC under reservation
  fulfill ORDER                NPC processes the order and returns result
  cancel CUSTOMER ORDER        Return materials from an open order
  face ACTOR DIRECTION         north, south, east, west
  jump ACTOR COUNT             Record jumps since last crafting attempt
  pray ACTOR                   Record one prayer on current game day
  days COUNT                   Advance in-game calendar
  add COUNT                    Add 1..100 primary elements (legacy action)
  quit                         Exit interactive console
Craft operations: extract, refine, mix, forge, carve, assemble, decorate,
                  brew, cook, weave, engineer, attune.
Existing elements.md and alchemy.md are preserved; mutable economy is in world_state.md.
)";
}

std::uint64_t number(const std::string& value) { return parse_u64(value); }
int small_number(const std::string& value) {
    const auto n = parse_int(value);
    if (n < 0) throw std::runtime_error("Expected non-negative number");
    return n;
}
void args_between(const std::vector<std::string>& v, std::size_t lo, std::size_t hi) {
    if (v.size() < lo || v.size() > hi) {
        throw std::runtime_error("Wrong argument count. Use 'help' for syntax");
    }
}
void print_actors(const rpg::GameState& s) {
    std::cout << "Actors (all use one crafting engine):\n";
    for (const auto& a : s.actors) {
        std::cout << "  " << a.id << " " << a.name << (a.npc ? " [NPC]" : " [PLAYER]");
        for (const auto& k : s.skills) if (k.actor == a.id)
            std::cout << " | " << rpg::name(k.profession) << " L" << rpg::level_from_xp(k.xp);
        std::cout << '\n';
    }
}
void print_nodes(const rpg::GameState& s) {
    std::cout << "Finite resource deposits:\n";
    for (const auto& n : s.nodes) std::cout << "  " << n.id << " " << rpg::name(n.kind)
        << " remaining=" << n.remaining << " element=" << n.element << '\n';
}
void print_inventory(const rpg::GameState& s, std::uint64_t owner) {
    const auto& a = s.actor(owner);
    std::cout << "Inventory of " << a.name << " (" << owner << "):\n";
    int count = 0;
    for (const auto& l : s.lots) if (l.owner == owner && l.quantity) {
        std::cout << "  lot#" << l.id << " " << rpg::name(l.kind) << " x" << l.quantity
            << " element=" << l.element << " quality=" << l.quality
            << " form=" << l.form << " from=[" << l.source1 << "," << l.source2 << "]\n";
        ++count;
    }
    for (const auto& i : s.items) if (i.owner == owner) {
        std::cout << "  item#" << i.id << " " << rpg::name(i.kind) << " quality=" << i.quality
            << " anomaly=" << i.anomaly << " decorated=" << i.decorated << '\n';
        ++count;
    }
    if (!count) std::cout << "  [empty]\n";
}
void inspect(const rpg::GameState& s, std::uint64_t id) {
    const auto& i = s.item(id);
    std::cout << "item#" << i.id << " " << rpg::name(i.kind) << " owner=" << i.owner
        << " quality=" << i.quality << " hardness=" << i.hardness
        << " comfort=" << i.comfort << " resonance=" << i.resonance
        << " decorated=" << i.decorated << " attuned=" << i.attuned
        << " anomaly=" << i.anomaly << "\n"
        << "  component1=" << i.component1 << " component2=" << i.component2
        << " decoration_lot=" << i.decoration_lot
        << " attunement_lot=" << i.attunement_lot << "\n";
}
void print_orders(const rpg::GameState& s) {
    for (const auto& o : s.orders) std::cout << "order#" << o.id
        << " customer=" << o.customer << " worker=" << o.worker
        << " op=" << rpg::name(o.operation) << " [" << o.input1 << "," << o.input2
        << "] " << rpg::name(o.status) << " output=" << o.output << '\n';
}
void print_discoveries(const rpg::GameState& s) {
    for (const auto& d : s.discoveries) std::cout << "discovery#" << d.id
        << " actor=" << d.actor << " item=" << d.item
        << " effect=" << d.effect << " day=" << d.day << '\n';
}
void print_knowledge(const rpg::GameState& s) {
    if (s.knowledge.empty()) std::cout << "No researched effects yet.\n";
    for (const auto& k : s.knowledge) std::cout << "actor=" << k.actor
        << " effect=" << k.effect << " observations=" << k.observations << '\n';
}
void print_result(const rpg::Result& r) {
    std::cout << (r.success ? "SUCCESS: " : "FAILURE: ") << r.message;
    if (r.has_output) std::cout << " -> " << (r.output_is_item ? "item#" : "lot#") << r.output;
    if (r.anomaly) std::cout << " [ANOMALY]";
    std::cout << '\n';
}
void print_history(const rpg::GameState& s, const ElementList& world) {
    const fs::path p = legacy_history_filename;
    if (fs::exists(p)) {
        // Исторические опыты версии 2 сохраняются и остаются доступными для чтения.
        const auto legacy = alchemy::load_history(p, world);
        std::cout << "Legacy v2 experiments:\n";
        alchemy::print_history(std::cout, legacy);
    }
    std::cout << "v3 crafting events (" << s.events.size() << "):\n";
    for (const auto& e : s.events) std::cout << "  #" << e.id << " day=" << e.day
        << " actor=" << e.actor << " " << e.verb << " inputs=[" << e.input1
        << "," << e.input2 << "] output=" << e.output << " "
        << (e.success ? "ok" : "failed") << '\n';
}

bool execute(const std::vector<std::string>& v, ElementList& world, rpg::GameState& state) {
    if (v.empty()) return true;
    const std::string& cmd = v[0];
    if (cmd == "help") { args_between(v,1,1); help(); return true; }
    if (cmd == "quit" || cmd == "exit") { args_between(v,1,1); return false; }
    if (cmd == "elements") { args_between(v,1,1); print(world); return true; }
    if (cmd == "actors") { args_between(v,1,1); print_actors(state); return true; }
    if (cmd == "nodes") { args_between(v,1,1); print_nodes(state); return true; }
    if (cmd == "inventory") { args_between(v,2,2); print_inventory(state,number(v[1])); return true; }
    if (cmd == "inspect") { args_between(v,2,2); inspect(state,number(v[1])); return true; }
    if (cmd == "orders") { args_between(v,1,1); print_orders(state); return true; }
    if (cmd == "discoveries") { args_between(v,1,1); print_discoveries(state); return true; }
    if (cmd == "knowledge") { args_between(v,1,1); print_knowledge(state); return true; }
    if (cmd == "history") { args_between(v,1,1); print_history(state,world); return true; }
    if (cmd == "odds") {
        args_between(v,3,3);
        alchemy::print_odds(std::cout,world,number(v[1]),number(v[2]));
        std::cout << "Note: skill may improve these odds in the v3 inventory-based game.\n";
        return true;
    }
    if (cmd == "add") {
        args_between(v,2,2);
        const auto amount = number(v[1]);
        if (amount < 1 || amount > 100) throw std::runtime_error("Elements count must be 1..100");
        ElementGenerator generator;
        generate_more(world,generator,static_cast<int>(amount));
        save(elements_filename, world);
        std::cout << "Expanded elements to " << world.size() << '\n';
        return true;
    }

    // Транзакция: сначала изменение копии, потом единый снимок на диск.
    // При неверных входных данных и ошибке файловой записи исходный state не меняется.
    rpg::GameState proposed = state;
    rpg::Engine engine(proposed, world);
    if (cmd == "gather") {
        args_between(v,3,3);
        print_result(engine.gather(number(v[1]),number(v[2])));
    } else if (cmd == "craft") {
        args_between(v,4,5);
        print_result(engine.perform(number(v[1]),rpg::operation(v[2]),number(v[3]),
                                   v.size() == 5 ? number(v[4]) : 0));
    } else if (cmd == "mix") {
        args_between(v,3,3);
        print_result(engine.perform(1,rpg::Operation::Mix,number(v[1]),number(v[2])));
    } else if (cmd == "give-lot") {
        args_between(v,4,5);
        const auto id = engine.give_lot(number(v[1]),number(v[2]),number(v[3]),
                                        v.size() == 5 ? small_number(v[4]) : 1);
        std::cout << "Transferred lot#" << id << '\n';
    } else if (cmd == "give-item") {
        args_between(v,4,4);
        engine.give_item(number(v[1]),number(v[2]),number(v[3]));
        std::cout << "Transferred item#" << v[3] << '\n';
    } else if (cmd == "order") {
        args_between(v,5,6);
        const auto id = engine.create_order(number(v[1]),number(v[2]),rpg::operation(v[3]),
                           number(v[4]),v.size() == 6 ? number(v[5]) : 0);
        std::cout << "Created NPC work order#" << id << '\n';
    } else if (cmd == "fulfill") {
        args_between(v,2,2);
        print_result(engine.fulfill_order(number(v[1])));
    } else if (cmd == "cancel") {
        args_between(v,3,3);
        engine.cancel_order(number(v[1]),number(v[2]));
        std::cout << "Cancelled order and returned inputs\n";
    } else if (cmd == "face") {
        args_between(v,3,3); engine.face(number(v[1]),v[2]);
    } else if (cmd == "jump") {
        args_between(v,3,3); engine.jump(number(v[1]),small_number(v[2]));
    } else if (cmd == "pray") {
        args_between(v,2,2); engine.pray(number(v[1]));
    } else if (cmd == "research") {
        args_between(v,4,4);
        const bool learned = engine.research(number(v[1]),number(v[2]),rpg::profession(v[3]));
        std::cout << (learned ? "Research succeeded; effect recorded\n" : "Research found nothing\n");
    } else if (cmd == "teach") {
        args_between(v,4,4);
        engine.teach(number(v[1]),number(v[2]),v[3]);
        std::cout << "Knowledge shared\n";
    } else if (cmd == "days") {
        args_between(v,2,2); engine.advance_days(small_number(v[1]));
        std::cout << "Game day: " << proposed.day << '\n';
    } else throw std::runtime_error("Unknown command: " + cmd);
    rpg::save_state(state_filename, proposed, world);
    state = std::move(proposed);
    return true;
}

void interactive(ElementList& world, rpg::GameState& state) {
    std::cout << "RPG workshop. Commands: help, actors, nodes, inventory 1, quit\n";
    std::string line;
    while (std::cout << "rpg> " && std::getline(std::cin,line)) {
        std::istringstream in(line);
        std::vector<std::string> words;
        for (std::string word; in >> word;) words.push_back(std::move(word));
        try { if (!execute(words, world, state)) return; }
        catch (const std::exception& e) { std::cout << "Error: " << e.what() << '\n'; }
    }
}
} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") { help(); return 0; }
        recover_backup_if_needed(elements_filename);
        ElementList world;
        if (fs::exists(elements_filename)) world = load(elements_filename);
        else {
            ElementGenerator generator;
            generate_more(world, generator, 12);
            save(elements_filename,world);
            std::cout << "Generated 12 primary elements in elements.md\n";
        }
        md_store::recover_backup_if_needed(state_filename);
        rpg::GameState state;
        if (fs::exists(state_filename)) state = rpg::load_state(state_filename,world);
        else {
            std::random_device rng;
            std::uint64_t seed = (static_cast<std::uint64_t>(rng()) << 32) ^ rng();
            if (!seed) seed = 1;
            state = rpg::new_game(world,seed);
            rpg::save_state(state_filename,state,world);
            std::cout << "Created world_state.md (player and 11 NPCs)\n";
        }
        md_store::recover_backup_if_needed(legacy_history_filename);
        if (argc == 1) {
            std::cout << "RPG Crafting Core v3, day " << state.day
                << ", lots=" << state.lots.size() << ", items=" << state.items.size()
                << ", orders=" << state.orders.size() << ". Use --play or --help.\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--play") {
            interactive(world,state); return 0;
        }
        std::vector<std::string> words;
        for (int i = 1; i < argc; ++i) {
            std::string s = argv[i];
            if (i == 1 && s.rfind("--",0) == 0) s = s.substr(2);
            words.push_back(std::move(s));
        }
        execute(words,world,state);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
