#pragma once
// Windows, in-process. All reads/writes are SEH-guarded so a bad pointer
// returns false instead of crashing the game.
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/pattern.hpp"

namespace game {

bool safe_read(uintptr_t addr, void* out, size_t size);
bool safe_write(uintptr_t addr, const void* in, size_t size);

template <class T>
std::optional<T> rd(uintptr_t addr) {
    T v{};
    if (!safe_read(addr, &v, sizeof(T))) return std::nullopt;
    return v;
}

struct Module {
    uintptr_t base = 0;
    size_t size = 0;  // SizeOfImage
    uint32_t time_date_stamp = 0;
    bool contains(uintptr_t a) const { return a >= base && a - base < size; }
};

// Loaded module by name; headers are validated (MZ, PE, x64). nullopt if absent/invalid.
std::optional<Module> find_module(const wchar_t* name);

enum class ScanStatus { Found, NotFound, Ambiguous };
struct ScanResult {
    ScanStatus status = ScanStatus::NotFound;
    uintptr_t address = 0;
};
// Scans the whole image; Found only when exactly one match exists.
ScanResult scan_unique(const Module& m, const core::Pattern& p);

}  // namespace game
