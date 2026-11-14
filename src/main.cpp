// DLL entry point. Everything heavy happens on a worker thread, never under the loader lock.
#include <windows.h>

#include <fstream>
#include <memory>
#include <sstream>

#include "app.hpp"
#include "hooks/hooks.hpp"
#include "util/log.hpp"

App& app() {
    static App a;
    return a;
}

void App::publish_profile() { skins.set_profile(std::make_shared<const core::Profile>(profile)); }

void App::save_profile() {
    const auto path = dir / "profile.ini";
    const auto tmp = dir / "profile.ini.tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << core::serialize_profile(profile);
        if (!out.good()) return;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);  // replaces atomically on the same volume
    if (ec) util::log("profile save failed: %s", ec.message().c_str());
}

namespace {

std::string read_all(const std::filesystem::path& p, bool& ok) {
    std::ifstream in(p, std::ios::binary);
    ok = static_cast<bool>(in);
    std::stringstream ss;
    if (ok) ss << in.rdbuf();
    return ss.str();
}

DWORD WINAPI worker(LPVOID module) {
    App& a = app();
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(static_cast<HMODULE>(module), buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return 1;
    a.dir = std::filesystem::path(buf).parent_path();
    util::log_init(a.dir / "skinchanger.log");
    util::log("---- start, dir=%s", a.dir.string().c_str());

    // The game loads client.dll after the process starts; wait for it (max 120 s).
    for (int i = 0; i < 1200 && !GetModuleHandleW(L"client.dll"); ++i) Sleep(100);

    a.skins.initialize(a.dir / "offsets.ini");

    bool ok = false;
    std::string tsv = read_all(a.dir / "skins.tsv", ok);
    if (ok) {
        a.db = core::parse_skin_db(tsv);
        a.db_note = std::to_string(a.db.weapons.size()) + " weapons, " + std::to_string(a.db.kits.size()) +
                    " finishes" + (a.db.errors.empty() ? "" : ", " + std::to_string(a.db.errors.size()) + " bad lines");
    } else {
        a.db_note = "skins.tsv not found";
    }
    std::string prof = read_all(a.dir / "profile.ini", ok);
    if (ok) a.profile = core::parse_profile(prof);
    a.publish_profile();

    if (!hooks::install(a.hook_error)) {
        util::log("hook install failed: %s", a.hook_error.c_str());
        a.active.store(false);
    }
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE t = CreateThread(nullptr, 0, worker, module, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
