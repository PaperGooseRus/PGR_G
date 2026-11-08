#include "elements.hpp"
#include "alchemy.hpp"
#include "alchemy_history.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

// Проверки должны работать и в Release, где стандартный assert отключён.
void require(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "TEST FAILED at line " << line << ": " << expression << "\n";
        std::exit(1);
    }
}
#define CHECK(expression) require(static_cast<bool>(expression), #expression, __LINE__)

ElementList example_world(int count = 12) {
    ElementList world;
    for (int id = 1; id <= count; ++id) {
        Element e;
        e.id = static_cast<std::uint64_t>(id);
        e.name = "Test-" + std::to_string(id);
        e.density = 2.0 + id;
        e.melting_c = 100;
        e.boiling_c = 200;
        e.hardness = 4;
        e.reactivity = 20;
        e.conductivity = 30;
        world.append(e);
    }
    return world;
}

void assert_odds(const ElementList& world, std::uint64_t a, std::uint64_t b,
                 int element, int variant, int nothing) {
    const auto p = alchemy::chances(world, a, b);
    CHECK(p.element == element);
    CHECK(p.variant == variant);
    CHECK(p.nothing == nothing);
    CHECK(element + variant + nothing == alchemy::probability_scale);
}

void test_probabilities() {
    const auto world = example_world();
    assert_odds(world, 1, 1, 10000, 0, 0);
    assert_odds(world, 1, 2, 5600, 2400, 2000);
    assert_odds(world, 2, 2, 4480, 1920, 3600);
    assert_odds(world, 6, 6, 750, 322, 8928);
    assert_odds(world, 6, 7, 0, 0, 10000);
    assert_odds(world, 2, 1, 5600, 2400, 2000); // Коммутативность.

    // Каждое увеличение целевого номера снижает вероятность успеха.
    int previous = alchemy::probability_scale;
    for (int target = 3; target <= 12; ++target) {
        const auto p = alchemy::chances(world, 1, static_cast<std::uint64_t>(target - 1));
        CHECK(p.element + p.variant < previous);
        previous = p.element + p.variant;
    }
}

void test_all_rolls() {
    const auto world = example_world();
    // Перебираем ВСЕ 10000 возможных чисел, а не полагаемся на статистику.
    for (auto recipe : {std::pair<int, int>{1, 1}, {1, 2}, {6, 6}, {6, 7}}) {
        const auto p = alchemy::chances(world, recipe.first, recipe.second);
        int elements = 0;
        int variants = 0;
        int failures = 0;
        for (int roll = 0; roll < alchemy::probability_scale; ++roll) {
            const auto r = alchemy::resolve(world, recipe.first, recipe.second,
                                            roll, "crystal");
            CHECK(alchemy::is_valid(world, r));
            switch (r.kind) {
            case alchemy::OutcomeKind::Element: ++elements; break;
            case alchemy::OutcomeKind::Variant: ++variants; break;
            case alchemy::OutcomeKind::Nothing: ++failures; break;
            }
        }
        CHECK(elements == p.element);
        CHECK(variants == p.variant);
        CHECK(failures == p.nothing);
    }

    // Все варианты меняют свойства, не повреждая исходные данные.
    const Element base = *alchemy::find_element(world, 2);
    const Element crystal = alchemy::configured_element(base, "crystal");
    const Element porous = alchemy::configured_element(base, "porous");
    const Element amorphous = alchemy::configured_element(base, "amorphous");
    CHECK(crystal.hardness > base.hardness);
    CHECK(porous.density < base.density);
    CHECK(amorphous.conductivity < base.conductivity);
    CHECK(alchemy::find_element(world, 2)->name == "Test-2");
}

void test_persistence() {
    namespace fs = std::filesystem;
    const fs::path folder = "alchemy_test_files";
    fs::remove_all(folder);
    fs::create_directories(folder);
    const fs::path elements_path = folder / "elements.md";
    const fs::path journal_path = folder / "alchemy.md";

    auto world = example_world();
    save(elements_path, world);
    auto loaded_world = load(elements_path);
    CHECK(loaded_world.size() == 12);
    CHECK(loaded_world.last_id() == 12);

    alchemy::History journal;
    journal.append(alchemy::resolve(loaded_world, 1, 1, 9999, "crystal"));
    journal.append(alchemy::resolve(loaded_world, 1, 2, 5600, "porous"));
    // 6+7 не существует в начальном мире, поэтому реакция неудачная.
    journal.append(alchemy::resolve(loaded_world, 6, 7, 0, "crystal"));
    alchemy::save_history(journal_path, journal);
    auto reloaded = alchemy::load_history(journal_path, loaded_world);
    CHECK(reloaded.size() == 3);
    CHECK(reloaded.head()->reaction.result_id == 2);
    CHECK(reloaded.head()->next->reaction.kind == alchemy::OutcomeKind::Variant);
    CHECK(reloaded.head()->next->reaction.form == "porous");
    CHECK(reloaded.head()->next->next->reaction.kind == alchemy::OutcomeKind::Nothing);

    // Расширение таблицы до 13 элементов не переписывает результат прошлого опыта.
    ElementGenerator generator;
    loaded_world.append(generator.generate(13));
    save(elements_path, loaded_world);
    auto expanded = load(elements_path);
    CHECK(expanded.size() == 13);
    CHECK(alchemy::load_history(journal_path, expanded).size() == 3);
    CHECK(alchemy::chances(expanded, 6, 7).element > 0);

    // Изменённая связь в Markdown должна приводить к ошибке, но файл остаётся нетронутым.
    std::ifstream file(elements_path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string broken = buffer.str();
    const std::size_t row_start = broken.find("| 1 | Test-1 | ");
    CHECK(row_start != std::string::npos);
    const std::size_t row_end = broken.find('\n', row_start);
    CHECK(row_end != std::string::npos);
    const std::size_t pos = broken.rfind("| 2 |", row_end);
    CHECK(pos != std::string::npos && pos > row_start);
    broken.replace(pos, 5, "| 99 |");
    {
        std::ofstream damaged(elements_path, std::ios::binary | std::ios::trunc);
        damaged << broken;
    }
    bool rejected = false;
    try { (void)load(elements_path); }
    catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    fs::remove_all(folder);
}

} // namespace

int main() {
    test_probabilities();
    test_all_rolls();
    test_persistence();
    std::cout << "All alchemy tests passed.\n";
}
