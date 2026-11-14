#pragma once
// Minimal thread-safe file logger. Call util::log_init(path) once; before that
// log calls are dropped.
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace util {

struct LogState {
    std::mutex m;
    FILE* f = nullptr;
};
inline LogState& log_state() {
    static LogState s;
    return s;
}

inline void log_init(const std::filesystem::path& path) {
    auto& s = log_state();
    std::lock_guard<std::mutex> g(s.m);
    if (s.f) return;
#ifdef _WIN32
    s.f = _wfopen(path.c_str(), L"ab");
#else
    s.f = std::fopen(path.string().c_str(), "ab");
#endif
}

inline void log(const char* fmt, ...) {
    auto& s = log_state();
    std::lock_guard<std::mutex> g(s.m);
    if (!s.f) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(s.f, fmt, ap);
    va_end(ap);
    std::fputc('\n', s.f);
    std::fflush(s.f);
}

}  // namespace util
