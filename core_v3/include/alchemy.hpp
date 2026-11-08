#pragma once

#include "elements.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <ostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>

namespace alchemy {

// Внутренние вероятности в сотых долях процента: 10000 = 100%.
constexpr int probability_scale = 10000;

enum class OutcomeKind { Nothing, Element, Variant };

inline std::string kind_name(OutcomeKind kind) {
    switch (kind) {
    case OutcomeKind::Nothing: return "nothing";
    case OutcomeKind::Element: return "element";
    case OutcomeKind::Variant: return "variant";
    }
    throw std::runtime_error("Unknown outcome kind");
}

inline OutcomeKind parse_kind(const std::string& text) {
    if (text == "nothing") return OutcomeKind::Nothing;
    if (text == "element") return OutcomeKind::Element;
    if (text == "variant") return OutcomeKind::Variant;
    throw std::runtime_error("Unknown result type: " + text);
}

inline const Element* find_element(const ElementList& list, std::uint64_t id) {
    for (const Node* n = list.head(); n; n = n->next.get()) {
        if (n->data.id == id) return &n->data;
    }
    return nullptr;
}

// Шансы реакции зависят только от индексов, а не от порядка реагентов.
struct Odds {
    std::uint64_t target_id = 0;
    int element = 0;
    int variant = 0;
    int nothing = probability_scale;
};

inline Odds chances(const ElementList& world, std::uint64_t a, std::uint64_t b,
                    std::uint64_t known_max) {
    if (a > known_max || b > known_max || !find_element(world, a) || !find_element(world, b)) {
        throw std::runtime_error("Both reagents must exist in elements.md");
    }
    if (a > std::numeric_limits<std::uint64_t>::max() - b) {
        throw std::runtime_error("Element index overflow");
    }

    Odds p;
    p.target_id = a + b;
    if (p.target_id > known_max || !find_element(world, p.target_id)) {
        // В текущем мире нет элемента с таким номером.
        return p;
    }
    if (a == 1 && b == 1) {
        // Обучающий рецепт: всегда создаёт элемент 2.
        p.element = probability_scale;
        p.nothing = 0;
        return p;
    }

    // Для каждого увеличения номера шанс успеха умножается на 0.8:
    // №3 -> 80%, №4 -> 64%, №12 -> ~10.7%.
    // Целочисленное вычисление исключает ошибки округления double.
    int success = probability_scale;
    for (std::uint64_t id = 2; id < p.target_id && success > 1; ++id) {
        success = std::max(1, success * 4 / 5);
    }
    p.element = std::max(1, success * 7 / 10);
    p.variant = find_element(world, p.target_id - 1) ? success - p.element : 0;
    p.nothing = probability_scale - p.element - p.variant;
    return p;
}

inline Odds chances(const ElementList& world, std::uint64_t a, std::uint64_t b) {
    return chances(world, a, b, world.last_id());
}

struct Reaction {
    std::uint64_t reagent_a = 0;
    std::uint64_t reagent_b = 0;
    std::uint64_t world_max = 0; // Максимальный известный ID в момент опыта.
    int roll = -1;  // Случайное значение от 0 до 9999.
    OutcomeKind kind = OutcomeKind::Nothing;
    std::uint64_t result_id = 0;
    std::string form = "-";
};

// На основе базового элемента создаём другую конфигурацию вещества.
// Изменения детерминированы: исходная таблица elements.md не изменяется.
inline Element configured_element(const Element& base, const std::string& form) {
    Element modified = base;
    if (form == "crystal") {
        modified.density = std::round(base.density * 1.05 * 100.0) / 100.0;
        modified.hardness = std::min(10, base.hardness + 2);
        modified.conductivity = std::min(100, base.conductivity + 15);
    } else if (form == "porous") {
        modified.density = std::round(base.density * 0.75 * 100.0) / 100.0;
        modified.density = std::max(0.01, modified.density);
        modified.hardness = std::max(1, base.hardness - 2);
        modified.reactivity = std::min(100, base.reactivity + 12);
    } else if (form == "amorphous") {
        modified.hardness = std::max(1, base.hardness - 1);
        modified.conductivity = std::max(0, base.conductivity - 10);
        modified.reactivity = std::min(100, base.reactivity + 18);
    } else {
        throw std::runtime_error("Unknown material form: " + form);
    }
    modified.name += " [" + form + "]";
    return modified;
}

// Валидация истории по тем же правилам, что применяются при эксперименте.
inline bool is_valid(const ElementList& world, const Reaction& r) {
    if (r.roll < 0 || r.roll >= probability_scale) return false;
    Odds p;
    if (r.world_max == 0 || r.world_max > world.last_id()) return false;
    try { p = chances(world, r.reagent_a, r.reagent_b, r.world_max); }
    catch (...) { return false; }
    if (r.roll < p.element) {
        return r.kind == OutcomeKind::Element && r.result_id == p.target_id
            && r.form == "-";
    }
    if (r.roll < p.element + p.variant) {
        return r.kind == OutcomeKind::Variant && r.result_id == p.target_id - 1
            && (r.form == "crystal" || r.form == "porous" || r.form == "amorphous")
            && find_element(world, r.result_id);
    }
    return r.kind == OutcomeKind::Nothing && r.result_id == 0 && r.form == "-";
}

// Чистая функция определения результата по выпавшему числу.
// Удобна для тестирования ВСЕХ граничных значений без случайности.
inline Reaction resolve(const ElementList& world, std::uint64_t a,
                        std::uint64_t b, int roll, const std::string& variant_form) {
    if (roll < 0 || roll >= probability_scale) {
        throw std::runtime_error("Roll must be in range 0..9999");
    }
    const Odds p = chances(world, a, b);
    Reaction r;
    r.reagent_a = a;
    r.reagent_b = b;
    r.world_max = world.last_id();
    r.roll = roll;
    if (roll < p.element) {
        r.kind = OutcomeKind::Element;
        r.result_id = p.target_id;
    } else if (roll < p.element + p.variant) {
        if (variant_form != "crystal" && variant_form != "porous"
            && variant_form != "amorphous") {
            throw std::runtime_error("Unknown variant form");
        }
        r.kind = OutcomeKind::Variant;
        r.result_id = p.target_id - 1;
        r.form = variant_form;
    }
    return r;
}

// Отдельный генератор отвечает только за реакцию, не изменяет мир.
class Alchemist {
public:
    Alchemist() : rng_(std::random_device{}()) {}
    explicit Alchemist(std::uint32_t seed) : rng_(seed) {} // Для тестов.

    Reaction mix(const ElementList& world, std::uint64_t a, std::uint64_t b) {
        // Проверяем реагенты до использования генератора случайных чисел.
        (void)chances(world, a, b);
        const int roll = std::uniform_int_distribution<int>(0, probability_scale - 1)(rng_);
        const char* forms[] = {"crystal", "porous", "amorphous"};
        const std::string form = forms[std::uniform_int_distribution<int>(0, 2)(rng_)];
        return resolve(world, a, b, roll, form);
    }

private:
    std::mt19937 rng_;
};

inline void print_odds(std::ostream& out, const ElementList& world,
                       std::uint64_t a, std::uint64_t b) {
    const Odds p = chances(world, a, b);
    out << "Recipe: " << a << " + " << b << " (target #" << p.target_id << ")\n";
    if (p.element == 0) out << "Target element does not exist in this world.\n";
    out << std::fixed << std::setprecision(2)
        << "  New element: " << static_cast<double>(p.element) / 100.0 << "%\n"
        << "  Alternative form of #" << (p.target_id >= 1 ? p.target_id - 1 : 0)
        << ": " << static_cast<double>(p.variant) / 100.0 << "%\n"
        << "  Nothing: " << static_cast<double>(p.nothing) / 100.0 << "%\n";
}

inline void print_reaction(std::ostream& out, const ElementList& world, const Reaction& r) {
    out << "Mix " << r.reagent_a << " + " << r.reagent_b
        << " [roll " << r.roll << "]: ";
    if (r.kind == OutcomeKind::Nothing) {
        out << "NOTHING produced.\n";
        return;
    }
    const Element* base = find_element(world, r.result_id);
    if (!base) throw std::runtime_error("Reaction refers to missing material");
    const Element value = r.kind == OutcomeKind::Variant
        ? configured_element(*base, r.form) : *base;
    out << kind_name(r.kind) << " #" << r.result_id << " " << value.name
        << " [density=" << std::fixed << std::setprecision(2) << value.density
        << ", hardness=" << value.hardness
        << ", reactivity=" << value.reactivity
        << ", conductivity=" << value.conductivity << "]\n";
}

} // namespace alchemy
