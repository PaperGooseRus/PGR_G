#include "crafting.hpp"
#include "alchemy_history.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok, const char* expression, int line) {
    if (!ok) { std::cerr << "FAIL line " << line << ": " << expression << '\n'; std::exit(1); }
}
#define CHECK(x) require(static_cast<bool>(x), #x, __LINE__)

template<typename Fn>
void reject(Fn fn) {
    bool thrown = false;
    try { fn(); } catch (const std::exception&) { thrown = true; }
    CHECK(thrown);
}
ElementList world_for_tests() {
    ElementList list;
    for (int id = 1; id <= 12; ++id) {
        Element e;
        e.id = id;
        e.name = "Synthetic-" + std::to_string(id);
        e.density = 3.0 + id;
        e.melting_c = 100;
        e.boiling_c = 350;
        e.hardness = 3 + id % 5;
        e.reactivity = 20;
        e.conductivity = 10 + 5 * id;
        list.append(e);
    }
    return list;
}

void test_no_material_from_nothing() {
    auto world = world_for_tests();
    auto s = rpg::new_game(world, 12345);
    rpg::Engine e(s, world, 1);
    CHECK(s.lots.empty());
    CHECK(s.items.empty());
    reject([&]{ e.perform(3, rpg::Operation::Forge, 400, 0, 0); });
    reject([&]{ e.perform(5, rpg::Operation::Decorate, 400, 401, 0); });
    reject([&]{ e.perform(3, rpg::Operation::Assemble, 400, 401, 0); });
    reject([&]{ e.give_item(3, 5, 500); });
    CHECK(s.lots.empty());
    CHECK(s.items.empty());
    CHECK(s.events.empty());
    CHECK(s.nodes[0].remaining == 40);
    CHECK(s.level(1, rpg::Profession::Blacksmith) == 1);
    CHECK(s.level(3, rpg::Profession::Blacksmith) == 10);
}

void test_chain_with_npcs_and_ownership() {
    auto world = world_for_tests();
    auto s = rpg::new_game(world, 222);
    rpg::Engine e(s, world, 1);
    // От сырья до меча: без бесконечных запасов у NPC.
    const auto ore = e.gather(7, 1).output;
    const auto wood = e.gather(6, 2).output;
    const auto gem = e.gather(7, 3).output;
    CHECK(s.node(1).remaining == 39);
    CHECK(s.node(2).remaining == 39);
    CHECK(s.node(3).remaining == 11);
    CHECK(s.lot(ore).kind == rpg::MaterialKind::Ore);
    CHECK(s.lot(wood).kind == rpg::MaterialKind::Wood);
    CHECK(s.lot(gem).kind == rpg::MaterialKind::Gem);
    e.give_lot(7, 2, ore);
    reject([&]{ e.perform(7, rpg::Operation::Refine, ore, 0, 0); });
    const auto metal = e.perform(2, rpg::Operation::Refine, ore, 0, 0).output;
    CHECK(s.lot(ore).quantity == 0);
    CHECK(s.lot(ore).owner == 0);
    CHECK(s.lot(metal).source1 == ore);
    e.give_lot(2, 3, metal);
    const auto blade = e.perform(3, rpg::Operation::Forge, metal, 0, 0, 9999).output;
    CHECK(s.item(blade).kind == rpg::ItemKind::Blade);
    CHECK(s.lot(metal).quantity == 0);
    e.give_lot(6, 4, wood);
    const auto handle = e.perform(4, rpg::Operation::Carve, wood, 0, 0, 9999).output;
    CHECK(s.item(handle).kind == rpg::ItemKind::Handle);
    reject([&]{ e.perform(3, rpg::Operation::Assemble, blade, handle, 0); });
    e.give_item(4, 3, handle);
    const auto sword = e.perform(3, rpg::Operation::Assemble, blade, handle, 0).output;
    CHECK(s.item(sword).kind == rpg::ItemKind::Sword);
    CHECK(s.item(sword).component1 == blade);
    CHECK(s.item(sword).component2 == handle);
    CHECK(s.item(blade).owner == 0);
    CHECK(s.item(handle).owner == 0);
    reject([&]{ e.give_item(3, 5, blade); });
    reject([&]{ e.perform(3, rpg::Operation::Assemble, blade, handle, 0); });
    e.give_item(3, 5, sword);
    e.give_lot(7, 5, gem);
    const auto decorated = e.perform(5, rpg::Operation::Decorate, sword, gem, 0).output;
    CHECK(decorated == sword);  // Украсили именно существующий меч!
    CHECK(s.item(sword).decorated);
    CHECK(s.item(sword).decoration_lot == gem);
    CHECK(s.lot(gem).quantity == 0);
    reject([&]{ e.perform(5, rpg::Operation::Decorate, sword, gem, 0); });
    CHECK(s.events.size() > 10);
    rpg::validate_state(s, world);
}

void test_orders_and_failures() {
    auto world = world_for_tests();
    auto s = rpg::new_game(world, 333);
    rpg::Engine e(s, world, 1);
    const auto ore = e.gather(1, 1).output;
    reject([&]{ e.create_order(1, 3, rpg::Operation::Forge, ore); });
    CHECK(s.lot(ore).owner == 1);
    const auto work = e.create_order(1, 2, rpg::Operation::Refine, ore);
    CHECK(s.order(work).status == rpg::OrderStatus::Open);
    CHECK(s.lot(ore).owner == 2);
    reject([&]{ e.give_lot(2, 3, ore); });  // заказ резервирует материалы
    reject([&]{ e.perform(2, rpg::Operation::Refine, ore, 0, 0); });
    const auto result = e.fulfill_order(work, 0);
    CHECK(result.success && !result.output_is_item);
    CHECK(s.lot(result.output).owner == 1);
    CHECK(s.order(work).status == rpg::OrderStatus::Fulfilled);
    reject([&]{ e.fulfill_order(work, 0); });
    const auto order2 = e.create_order(1, 3, rpg::Operation::Forge, result.output);
    const auto failure = e.fulfill_order(order2, 9999);
    CHECK(!failure.success);
    CHECK(s.order(order2).status == rpg::OrderStatus::Failed);
    CHECK(s.lot(result.output).quantity == 0); // нет дублирования после брака
    reject([&]{ e.fulfill_order(order2, 0); });
    reject([&]{ e.create_order(1, 3, rpg::Operation::Forge, result.output); });
    rpg::validate_state(s, world);
}

void test_cancel_order_and_safe_transfer() {
    auto world = world_for_tests();
    auto s = rpg::new_game(world, 1717);
    rpg::Engine e(s, world, 1);
    auto ore = e.gather(1, 1).output;
    auto order = e.create_order(1, 2, rpg::Operation::Refine, ore);
    reject([&]{ e.cancel_order(7, order); });
    CHECK(s.lot(ore).owner == 2);
    e.cancel_order(1, order);
    CHECK(s.order(order).status == rpg::OrderStatus::Cancelled);
    CHECK(s.lot(ore).owner == 1);
    reject([&]{ e.fulfill_order(order, 0); });
    auto new_order = e.create_order(1, 2, rpg::Operation::Refine, ore);
    CHECK(s.order(new_order).status == rpg::OrderStatus::Open);
    rpg::validate_state(s, world);
}

void test_order_assembly_refund() {
    auto world = world_for_tests();
    auto s = rpg::new_game(world, 444);
    rpg::Engine e(s, world, 1);
    auto ore = e.gather(1, 1).output;
    auto wood = e.gather(1, 2).output;
    auto metal = e.perform(1, rpg::Operation::Refine, ore, 0, 0).output;
    auto blade = e.perform(1, rpg::Operation::Forge, metal, 0, 0, 9999).output;
    auto handle = e.perform(1, rpg::Operation::Carve, wood, 0, 0, 9999).output;
    auto order = e.create_order(1, 3, rpg::Operation::Assemble, blade, handle);
    CHECK(s.item(blade).owner == 3 && s.item(handle).owner == 3);
    const auto failed = e.fulfill_order(order, 9999);
    CHECK(!failed.success);
    CHECK(s.item(blade).owner == 1 && s.item(handle).owner == 1);
    auto again = e.create_order(1, 3, rpg::Operation::Assemble, blade, handle);
    const auto passed = e.fulfill_order(again, 0);
    CHECK(passed.success);
    CHECK(s.item(passed.output).owner == 1);
    CHECK(s.item(blade).owner == 0 && s.item(handle).owner == 0);
    rpg::validate_state(s, world);
}

void test_alchemy_legacy_rules_and_inventory() {
    auto world = world_for_tests();
    auto s = rpg::new_game(world, 555);
    rpg::Engine e(s, world, 1);
    auto ore1 = e.gather(1, 6).output;
    auto ore2 = e.gather(1, 6).output;
    auto el1 = e.perform(1, rpg::Operation::Extract, ore1, 0, 0).output;
    auto el2 = e.perform(1, rpg::Operation::Extract, ore2, 0, 0).output;
    CHECK(s.lot(el1).element == 1 && s.lot(el2).element == 1);
    CHECK(s.node(6).remaining == 62);
    reject([&]{ e.perform(1, rpg::Operation::Refine, el1, 0, 0); });
    const auto mix = e.perform(1, rpg::Operation::Mix, el1, el2, 9999);
    CHECK(mix.success); // 1+1 всегда даёт №2 независимо от уровня.
    CHECK(s.lot(mix.output).element == 2);
    CHECK(s.lot(mix.output).kind == rpg::MaterialKind::Element);
    CHECK(s.lot(el1).quantity == 0 && s.lot(el2).quantity == 0);
    reject([&]{ e.perform(1, rpg::Operation::Mix, el1, el2, 0); });
    CHECK(s.level(1, rpg::Profession::Alchemist) > 1);
    rpg::validate_state(s,world);
}

void test_other_professions() {
    auto world = world_for_tests();
    auto s = rpg::new_game(world, 777);
    rpg::Engine e(s, world, 1);
    const auto herb = e.gather(8, 4).output;
    CHECK(e.perform(8, rpg::Operation::Brew, herb, 0, 0).success);
    const auto herb2 = e.gather(9, 4).output;
    CHECK(s.lot(e.perform(9, rpg::Operation::Cook, herb2, 0, 0).output).kind == rpg::MaterialKind::Food);
    const auto fiber = e.gather(10, 5).output;
    CHECK(s.lot(e.perform(10, rpg::Operation::Weave, fiber, 0, 0).output).kind == rpg::MaterialKind::Cloth);
    const auto ore = e.gather(11, 1).output;
    const auto wood = e.gather(11, 2).output;
    const auto metal = e.perform(11, rpg::Operation::Refine, ore, 0, 0).output;
    CHECK(s.item(e.perform(11, rpg::Operation::Engineer, metal, wood, 0).output).kind == rpg::ItemKind::Device);
    rpg::validate_state(s,world);
}

void test_secret_and_persistence() {
    auto world = world_for_tests();
    auto s = rpg::new_game(world, 123456789ULL);
    rpg::Engine e(s, world, 1);
    const auto condition = rpg::hidden_condition(s.seed);
    CHECK(condition.jumps >= 2 && condition.jumps <= 6);
    CHECK(condition.prayer_days >= 3 && condition.prayer_days <= 7);
    auto ore = e.gather(1, 1).output;
    auto metal = e.perform(1, rpg::Operation::Refine, ore, 0, 0).output;
    for (int day = 1; day <= condition.prayer_days; ++day) {
        if (day > 1) e.advance_days(1);
        e.pray(1);
    }
    e.face(1, condition.facing);
    e.jump(1, condition.jumps);
    const auto result = e.perform(1, rpg::Operation::Forge, metal, 0, 0, 0);
    CHECK(result.anomaly);
    CHECK(s.item(result.output).anomaly);
    CHECK(s.discoveries.size() == 1);
    CHECK(e.research(1, result.output, rpg::Profession::Alchemist, 0));
    CHECK(s.knowledge.size() == 1);
    CHECK(s.knowledge.front().effect == "structural-memory");
    e.teach(1, 3, "structural-memory");
    CHECK(s.knowledge.size() == 2);
    CHECK(s.knowledge[1].actor == 3);
    reject([&]{ e.teach(7, 4, "structural-memory"); });
    // Изучив эффект, мастер всё ещё обязан соблюсти условия,
    // но его шанс повторить открытие выше (0,25% вместо 0,05%).
    auto next_ore = e.gather(1, 1).output;
    auto next_metal = e.perform(1, rpg::Operation::Refine, next_ore, 0, 0).output;
    e.jump(1, condition.jumps);
    const auto repeated = e.perform(1, rpg::Operation::Forge, next_metal, 0, 0, 20);
    CHECK(repeated.anomaly);
    CHECK(s.discoveries.size() == 2);
    const auto copy = rpg::hidden_condition(s.seed);
    CHECK(copy.facing == condition.facing);
    const std::filesystem::path dir = "crafting_test_files";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto path = dir / "world_state.md";
    rpg::save_state(path,s,world);
    auto loaded = rpg::load_state(path,world);
    CHECK(loaded.seed == s.seed);
    CHECK(loaded.day == s.day);
    CHECK(loaded.discoveries.size() == 2);
    CHECK(loaded.knowledge.size() == 2);
    CHECK(loaded.item(result.output).anomaly);
    CHECK(loaded.actor(1).jumps == 0);
    CHECK(loaded.skills.size() == s.skills.size());
    // Проверяем отказ при намеренной порче ссылки.
    std::ifstream read(path);
    std::stringstream buffer; buffer << read.rdbuf();
    auto data = buffer.str();
    auto needle = "| " + std::to_string(result.output) + " | 1 | blade | " + std::to_string(metal) + " |";
    auto pos = data.find(needle);
    CHECK(pos != std::string::npos);
    data.replace(pos, needle.size(), "| " + std::to_string(result.output) + " | 1 | blade | 999999 |\n");
    { std::ofstream out(path, std::ios::trunc); out << data; }
    reject([&]{ (void)rpg::load_state(path,world); });
    std::filesystem::remove_all(dir);
}

void test_finite_resources() {
    auto world = world_for_tests();
    auto s = rpg::new_game(world, 7777);
    rpg::Engine e(s, world, 2);
    s.node(1).remaining = 1;
    CHECK(e.gather(7,1).success);
    reject([&]{ e.gather(7,1); });
    CHECK(s.node(1).remaining == 0);
    rpg::validate_state(s,world);
}

} // namespace

int main() {
    test_no_material_from_nothing();
    test_chain_with_npcs_and_ownership();
    test_orders_and_failures();
    test_cancel_order_and_safe_transfer();
    test_order_assembly_refund();
    test_alchemy_legacy_rules_and_inventory();
    test_other_professions();
    test_secret_and_persistence();
    test_finite_resources();
    std::cout << "All v3 crafting tests passed.\n";
}
