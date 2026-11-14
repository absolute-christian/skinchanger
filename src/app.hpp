#pragma once
#include <atomic>
#include <filesystem>
#include <string>

#include "core/profile.hpp"
#include "core/skin_db.hpp"
#include "game/skins.hpp"

// Single process-wide state. `profile` and `db` are owned by the render thread
// (UI) after initialisation; the game thread only ever sees immutable
// snapshots published through SkinChanger::set_profile().
struct App {
    std::filesystem::path dir;  // directory of the DLL: offsets.ini, skins.tsv, profile.ini, log
    game::SkinChanger skins;
    core::SkinDb db;
    core::Profile profile;

    std::atomic<bool> menu_visible{false};
    std::atomic<bool> active{true};  // false = every hook passes straight through
    std::atomic<bool> present_hook_ok{false};
    std::atomic<bool> fsn_hook_ok{false};
    std::string hook_error;  // written once during install, read-only afterwards
    std::string db_note;     // skins.tsv load result

    void publish_profile();
    void save_profile();
};

App& app();
