#pragma once

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

// Общее хранилище Markdown: модули не знают форматов друг друга.
namespace md_store {
namespace fs = std::filesystem;

// Сохранение через временный файл и резервную копию.
// При ошибке сохранения исходный файл по возможности восстанавливается.
template <typename WriteFunction>
void save_atomically(const fs::path& file, WriteFunction write) {
    const fs::path temporary = file.string() + ".tmp";
    const fs::path backup = file.string() + ".bak";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot open temporary file: " + temporary.string());
        write(out);
        out.flush();
        if (!out) throw std::runtime_error("Cannot write Markdown: " + temporary.string());
    }

    const bool had_original = fs::exists(file);
    if (had_original) {
        if (fs::exists(backup)) {
            throw std::runtime_error("Backup already exists: " + backup.string());
        }
        fs::rename(file, backup);
    }
    try {
        fs::rename(temporary, file);
    } catch (...) {
        if (had_original) {
            std::error_code ignored;
            fs::rename(backup, file, ignored);
        }
        throw;
    }
    if (had_original) {
        std::error_code ignored;
        fs::remove(backup, ignored);
    }
}

inline void recover_backup_if_needed(const fs::path& file) {
    const fs::path backup = file.string() + ".bak";
    if (!fs::exists(file) && fs::exists(backup)) {
        fs::rename(backup, file);
    }
}
} // namespace md_store
