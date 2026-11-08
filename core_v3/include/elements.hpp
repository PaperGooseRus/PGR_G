#pragma once

#include "storage.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

// Этап 1: вымышленные химические элементы игрового мира.
// Реальная химия здесь не моделируется: значения являются характеристиками RPG.
struct Element {
    std::uint64_t id = 0;
    std::string name;
    double density = 0.0;         // г/см^3
    int melting_c = 0;            // температура плавления, °C
    int boiling_c = 0;            // температура кипения, °C
    int hardness = 0;             // условная шкала 1..10
    int reactivity = 0;           // игровая шкала 0..100
    int conductivity = 0;         // игровая шкала 0..100
};

// Узел ОДНОсвязного списка: в памяти ссылка хранится как указатель.
struct Node {
    explicit Node(Element element) : data(std::move(element)) {}

    Element data;
    std::unique_ptr<Node> next;
};

class ElementList {
public:
    void append(Element element) {
        auto node = std::make_unique<Node>(std::move(element));
        Node* new_tail = node.get();

        if (tail_) {
            tail_->next = std::move(node);
        } else {
            head_ = std::move(node);
        }

        tail_ = new_tail;
        ++size_;
    }

    const Node* head() const { return head_.get(); }
    std::size_t size() const { return size_; }
    std::uint64_t last_id() const { return tail_ ? tail_->data.id : 0; }

    std::uint64_t next_id() const {
        if (!tail_) return 1;
        if (tail_->data.id == std::numeric_limits<std::uint64_t>::max()) {
            throw std::runtime_error("Element ID space exhausted");
        }
        return tail_->data.id + 1;
    }

private:
    std::unique_ptr<Node> head_;
    Node* tail_ = nullptr;        // Не владеет памятью узла
    std::size_t size_ = 0;
};

class ElementGenerator {
public:
    ElementGenerator() : rng_(std::random_device{}()) {}

    Element generate(std::uint64_t id) {
        static constexpr const char* prefixes[] = {
            "Aer", "Vor", "Cry", "Lun", "Zer", "Fer", "Tal", "Mor",
            "Syl", "Or", "Vel", "Nyx", "Dar", "Hel", "Qua", "Ith"
        };
        static constexpr const char* suffixes[] = {
            "ium", "on", "ite", "ar", "ene", "yx", "orium", "elis"
        };

        const auto prefix_index = random_int(0, 15);
        const auto suffix_index = random_int(0, 7);

        Element element;
        element.id = id;
        // ID в имени гарантирует уникальность даже при совпадении слогов.
        element.name = std::string(prefixes[prefix_index]) + suffixes[suffix_index]
                       + "-" + std::to_string(id);
        element.density = random_real(0.10, 24.00);
        element.melting_c = random_int(-200, 3500);
        element.boiling_c = element.melting_c + random_int(50, 2200);
        element.hardness = random_int(1, 10);
        element.reactivity = random_int(0, 100);
        element.conductivity = random_int(0, 100);
        return element;
    }

private:
    int random_int(int min, int max) {
        return std::uniform_int_distribution<int>(min, max)(rng_);
    }

    double random_real(double min, double max) {
        const double value = std::uniform_real_distribution<double>(min, max)(rng_);
        return std::round(value * 100.0) / 100.0;
    }

    std::mt19937 rng_;
};

// Удаляет только пробелы и табуляцию вокруг строкового значения.
inline std::string trim(const std::string& input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}

// Чтение строки Markdown-таблицы, например "| 1 | Aerium | 2 |".
inline std::vector<std::string> markdown_cells(const std::string& line) {
    std::vector<std::string> cells;
    if (line.size() < 2 || line.front() != '|' || line.back() != '|') {
        return cells;
    }

    std::size_t begin = 1;
    while (begin < line.size()) {
        const auto end = line.find('|', begin);
        if (end == std::string::npos) break;
        cells.push_back(trim(line.substr(begin, end - begin)));
        begin = end + 1;
    }
    return cells;
}

inline std::uint64_t parse_u64(const std::string& value) {
    std::size_t used = 0;
    if (value.empty() || value.front() == '-') {
        throw std::runtime_error("Expected non-negative integer: " + value);
    }
    const auto result = std::stoull(value, &used);
    if (used != value.size()) throw std::runtime_error("Invalid integer: " + value);
    return result;
}

inline int parse_int(const std::string& value) {
    std::size_t used = 0;
    const auto result = std::stoll(value, &used);
    if (used != value.size() || result < std::numeric_limits<int>::min()
        || result > std::numeric_limits<int>::max()) {
        throw std::runtime_error("Invalid int: " + value);
    }
    return static_cast<int>(result);
}

inline double parse_double(const std::string& value) {
    std::size_t used = 0;
    const auto result = std::stod(value, &used);
    if (used != value.size() || !std::isfinite(result)) {
        throw std::runtime_error("Invalid real number: " + value);
    }
    return result;
}

// На диске указатель next заменяется идентификатором следующего узла.
// Это позволяет восстановить связный список при следующем запуске.
inline void write_markdown(std::ostream& out, const ElementList& list) {
    out << "# RPG world elements\n\n";
    out << "<!-- RPG_ELEMENTS_V1 -->\n\n";
    out << "Each row is a linked-list node. `next` stores the next node's ID; `-` is null.\n\n";
    out << "| id | name | density_g_cm3 | melting_C | boiling_C | hardness | reactivity | conductivity | next |\n";
    out << "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n";
    out << std::fixed << std::setprecision(2);

    for (const Node* node = list.head(); node; node = node->next.get()) {
        const auto& e = node->data;
        out << "| " << e.id << " | " << e.name << " | " << e.density
            << " | " << e.melting_c << " | " << e.boiling_c
            << " | " << e.hardness << " | " << e.reactivity
            << " | " << e.conductivity << " | ";
        if (node->next) out << node->next->data.id;
        else out << '-';
        out << " |\n";
    }
}

// Совместимая с первой версией запись elements.md.
inline void save(const fs::path& file, const ElementList& list) {
    md_store::save_atomically(file, [&](std::ostream& out) {
        write_markdown(out, list);
    });
}

// Возвращает мир после прерванного сохранения.
inline void recover_backup_if_needed(const fs::path& file) {
    const bool was_missing = !fs::exists(file);
    md_store::recover_backup_if_needed(file);
    if (was_missing && fs::exists(file)) {
        std::cout << "Recovered previous data from " << file.string() << "\n";
    }
}

inline ElementList load(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read " + file.string());

    ElementList list;
    bool version_seen = false;
    bool table_seen = false;
    bool in_table = false;
    std::uint64_t previous_next_id = 0;
    std::uint64_t previous_id = 0;
    std::unordered_set<std::uint64_t> seen_ids;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(in, line)) {
        ++line_number;
        const std::string cleaned = trim(line);
        if (cleaned == "<!-- RPG_ELEMENTS_V1 -->") version_seen = true;

        const auto cells = markdown_cells(cleaned);
        if (cells.size() == 9 && cells[0] == "id" && cells[1] == "name") {
            table_seen = true;
            in_table = true;
            continue;
        }
        if (!in_table || cells.empty()) continue;
        if (cells[0] == "---") continue;
        if (cells.size() != 9) {
            throw std::runtime_error("Invalid table row at line " + std::to_string(line_number));
        }

        try {
            Element e;
            e.id = parse_u64(cells[0]);
            e.name = cells[1];
            e.density = parse_double(cells[2]);
            e.melting_c = parse_int(cells[3]);
            e.boiling_c = parse_int(cells[4]);
            e.hardness = parse_int(cells[5]);
            e.reactivity = parse_int(cells[6]);
            e.conductivity = parse_int(cells[7]);
            const std::uint64_t next_id = cells[8] == "-" ? 0 : parse_u64(cells[8]);

            if (e.id == 0 || e.name.empty() || e.density <= 0 ||
                e.boiling_c <= e.melting_c || e.hardness < 1 || e.hardness > 10 ||
                e.reactivity < 0 || e.reactivity > 100 ||
                e.conductivity < 0 || e.conductivity > 100) {
                throw std::runtime_error("Element properties are outside permitted ranges");
            }
            if (!seen_ids.insert(e.id).second) {
                throw std::runtime_error("Duplicate element ID");
            }
            if (list.size() != 0 && previous_next_id != e.id) {
                throw std::runtime_error("Broken next link in Markdown table");
            }
            if (list.size() != 0 && e.id <= previous_id) {
                throw std::runtime_error("Element IDs must increase along the list");
            }
            previous_id = e.id;
            previous_next_id = next_id;
            list.append(std::move(e));
        } catch (const std::exception& error) {
            throw std::runtime_error("Line " + std::to_string(line_number) + ": " + error.what());
        }
    }

    if (!in.eof() && in.fail()) throw std::runtime_error("Error reading Markdown file");
    if (!version_seen || !table_seen || list.size() == 0) {
        throw std::runtime_error("Missing version marker or element table");
    }
    if (previous_next_id != 0) {
        throw std::runtime_error("Last node must point to '-' (null)");
    }
    return list;
}

inline void print(const ElementList& list) {
    std::cout << "\nRPG elements: " << list.size() << "\n";
    std::cout << "ID    NAME                DENSITY  MELT(C)  BOIL(C)  HARD  REACT  COND  NEXT\n";
    for (const Node* node = list.head(); node; node = node->next.get()) {
        const auto& e = node->data;
        std::cout << std::left << std::setw(6) << e.id
                  << std::setw(20) << e.name
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(7) << e.density
                  << std::setw(9) << e.melting_c
                  << std::setw(9) << e.boiling_c
                  << std::setw(6) << e.hardness
                  << std::setw(7) << e.reactivity
                  << std::setw(6) << e.conductivity
                  << std::setw(6) << (node->next ? std::to_string(node->next->data.id) : "-")
                  << '\n';
    }
}

inline void generate_more(ElementList& list, ElementGenerator& generator, int count) {
    for (int i = 0; i < count; ++i) {
        list.append(generator.generate(list.next_id()));
    }
}

