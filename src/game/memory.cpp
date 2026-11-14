#include "game/memory.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace game {
namespace {

// No C++ objects in these two functions: __try cannot coexist with unwinding.
bool guarded_copy(void* dst, const void* src, size_t n) {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool safe_read(uintptr_t addr, void* out, size_t size) {
    if (!out || size == 0) return false;
    if (addr + size < addr) return false;  // wraparound
    return guarded_copy(out, reinterpret_cast<const void*>(addr), size);
}

bool safe_write(uintptr_t addr, const void* in, size_t size) {
    if (!in || size == 0) return false;
    if (addr + size < addr) return false;
    return guarded_copy(reinterpret_cast<void*>(addr), in, size);
}

std::optional<Module> find_module(const wchar_t* name) {
    HMODULE h = GetModuleHandleW(name);
    if (!h) return std::nullopt;
    const uintptr_t base = reinterpret_cast<uintptr_t>(h);

    IMAGE_DOS_HEADER dos{};
    if (!safe_read(base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return std::nullopt;
    if (dos.e_lfanew <= 0 || dos.e_lfanew > 0x1000) return std::nullopt;

    IMAGE_NT_HEADERS64 nt{};
    if (!safe_read(base + static_cast<uintptr_t>(dos.e_lfanew), &nt, sizeof nt)) return std::nullopt;
    if (nt.Signature != IMAGE_NT_SIGNATURE) return std::nullopt;
    if (nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return std::nullopt;
    if (nt.OptionalHeader.SizeOfImage == 0) return std::nullopt;

    Module m;
    m.base = base;
    m.size = nt.OptionalHeader.SizeOfImage;
    m.time_date_stamp = nt.FileHeader.TimeDateStamp;
    return m;
}

ScanResult scan_unique(const Module& m, const core::Pattern& p) {
    constexpr size_t kChunk = 1u << 20;
    const size_t n = p.bytes.size();
    ScanResult res;
    if (n == 0 || n > 512 || m.size < n) return res;

    std::vector<uint8_t> buf(kChunk + n - 1);
    size_t matches = 0;
    for (size_t off = 0; off < m.size && matches < 2; off += kChunk) {
        const size_t want = std::min(kChunk + n - 1, m.size - off);
        if (!safe_read(m.base + off, buf.data(), want)) continue;  // unreadable chunk: skip
        size_t from = 0;
        while (matches < 2) {
            auto hit = core::find_pattern(buf.data(), want, p, from);
            if (!hit || *hit >= kChunk) break;  // matches starting past kChunk belong to the next chunk
            ++matches;
            res.address = m.base + off + *hit;
            from = *hit + 1;
        }
    }
    res.status = matches == 0 ? ScanStatus::NotFound : (matches == 1 ? ScanStatus::Found : ScanStatus::Ambiguous);
    if (res.status != ScanStatus::Found) res.address = 0;
    return res;
}

}  // namespace game
