#pragma once

#include "alchemy.hpp"
#include "storage.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace alchemy {

// Отдельный односвязный список. История не связана с устройством ElementList.
struct HistoryNode {
    std::uint64_t attempt = 0;
    Reaction reaction;
    std::unique_ptr<HistoryNode> next;
};

class History {
public:
    void append(Reaction r) {
        if (last_id_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::runtime_error("History index overflow");
        }
        auto node = std::make_unique<HistoryNode>();
        node->attempt = ++last_id_;
        node->reaction = std::move(r);
        HistoryNode* raw = node.get();
        if (tail_) tail_->next = std::move(node);
        else head_ = std::move(node);
        tail_ = raw;
        ++size_;
    }

    const HistoryNode* head() const { return head_.get(); }
    std::size_t size() const { return size_; }

private:
    std::unique_ptr<HistoryNode> head_;
    HistoryNode* tail_ = nullptr;
    std::uint64_t last_id_ = 0;
    std::size_t size_ = 0;
};

inline void write_history(std::ostream& out, const History& history) {
    out << "# RPG alchemy experiments\n\n"
        << "<!-- RPG_ALCHEMY_V1 -->\n\n"
        << "Every row is a linked-list node; 'next' contains the ID of the next attempt.\n\n"
        << "| attempt | reagent_a | reagent_b | world_max | roll | result | element_id | form | next |\n"
        << "| ---: | ---: | ---: | ---: | ---: | --- | ---: | --- | ---: |\n";
    for (const HistoryNode* n = history.head(); n; n = n->next.get()) {
        const auto& r = n->reaction;
        out << "| " << n->attempt << " | " << r.reagent_a << " | " << r.reagent_b
            << " | " << r.world_max << " | " << r.roll << " | " << kind_name(r.kind)
            << " | " << r.result_id << " | " << r.form << " | ";
        if (n->next) out << n->next->attempt;
        else out << '-';
        out << " |\n";
    }
}

inline void save_history(const fs::path& file, const History& history) {
    md_store::save_atomically(file, [&](std::ostream& out) {
        write_history(out, history);
    });
}

inline History load_history(const fs::path& file, const ElementList& world) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read " + file.string());
    History history;
    bool version_seen = false;
    bool table_seen = false;
    bool in_table = false;
    std::uint64_t expected_next = 0;
    std::string line;
    std::size_t line_number = 0;

    while (std::getline(in, line)) {
        ++line_number;
        const std::string cleaned = trim(line);
        if (cleaned == "<!-- RPG_ALCHEMY_V1 -->") version_seen = true;
        const auto cells = markdown_cells(cleaned);
        if (cells.size() == 9 && cells[0] == "attempt" && cells[1] == "reagent_a") {
            table_seen = true;
            in_table = true;
            continue;
        }
        if (!in_table || cells.empty()) continue;
        if (cells[0] == "---:") continue;
        if (cells.size() != 9) {
            throw std::runtime_error("Invalid alchemy row at line " + std::to_string(line_number));
        }
        try {
            const std::uint64_t attempt = parse_u64(cells[0]);
            Reaction r;
            r.reagent_a = parse_u64(cells[1]);
            r.reagent_b = parse_u64(cells[2]);
            r.world_max = parse_u64(cells[3]);
            r.roll = parse_int(cells[4]);
            r.kind = parse_kind(cells[5]);
            r.result_id = parse_u64(cells[6]);
            r.form = cells[7];
            const std::uint64_t next = cells[8] == "-" ? 0 : parse_u64(cells[8]);
            if (attempt != history.size() + 1) {
                throw std::runtime_error("History attempts must be consecutive starting at 1");
            }
            if (history.size() && expected_next != attempt) {
                throw std::runtime_error("Broken next link in alchemy.md");
            }
            if (!is_valid(world, r)) {
                throw std::runtime_error("Reaction disagrees with rules or missing elements");
            }
            expected_next = next;
            history.append(std::move(r));
        } catch (const std::exception& e) {
            throw std::runtime_error("Line " + std::to_string(line_number) + ": " + e.what());
        }
    }
    if (!in.eof() && in.fail()) throw std::runtime_error("Error reading alchemy.md");
    if (!version_seen || !table_seen || (history.size() && expected_next != 0)) {
        throw std::runtime_error("Invalid alchemy.md version or linked-list tail");
    }
    return history;
}

inline void print_history(std::ostream& out, const History& history) {
    out << "\nExperiments: " << history.size() << "\n";
    for (const HistoryNode* n = history.head(); n; n = n->next.get()) {
        const auto& r = n->reaction;
        out << "#" << n->attempt << ": " << r.reagent_a << " + " << r.reagent_b
            << " -> " << kind_name(r.kind);
        if (r.result_id != 0) out << " #" << r.result_id;
        if (r.form != "-") out << " [" << r.form << "]";
        out << "\n";
    }
}

} // namespace alchemy
