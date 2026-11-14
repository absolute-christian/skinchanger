#include "ui/ui.hpp"

#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#include "app.hpp"
#include "assets/logo_png.hpp"
#include "hooks/hooks.hpp"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "util/log.hpp"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using Microsoft::WRL::ComPtr;

namespace {

// ---------------------------------------------------------------- palette
ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }
const ImVec4 kBg = rgb(13, 15, 22);
const ImVec4 kPanel = rgb(20, 23, 33);
const ImVec4 kPanel2 = rgb(27, 31, 44);
const ImVec4 kBorder = rgb(40, 45, 64);
const ImVec4 kAccent = rgb(124, 92, 255);
const ImVec4 kAccentHi = rgb(160, 136, 255);
const ImVec4 kText = rgb(228, 231, 240);
const ImVec4 kMuted = rgb(130, 137, 158);
const ImVec4 kOk = rgb(74, 222, 128);
const ImVec4 kBad = rgb(248, 113, 113);
const ImVec4 kWarn = rgb(250, 204, 21);

struct State {
    bool inited = false;
    bool failed = false;
    HWND hwnd = nullptr;
    WNDPROC orig_wndproc = nullptr;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11ShaderResourceView> logo;

    int page = 0;  // 0 skins, 1 status, 2 settings
    int selected_def = 0;
    char weapon_filter[48] = {};
    char kit_filter[48] = {};
    int add_def = 0;
    std::vector<int> kit_view;
    std::string kit_view_filter;
    bool kit_view_valid = false;
    bool was_visible = false;
    bool request_unload = false;
};
State g;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool contains_ci(const std::string& hay, const std::string& needle_lower) {
    return needle_lower.empty() || lower(hay).find(needle_lower) != std::string::npos;
}

// ---------------------------------------------------------------- theme
void apply_theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 14;
    s.ChildRounding = 12;
    s.FrameRounding = 8;
    s.GrabRounding = 8;
    s.PopupRounding = 8;
    s.ScrollbarRounding = 8;
    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(10, 10);
    s.ScrollbarSize = 10;
    s.WindowBorderSize = 1;
    s.ChildBorderSize = 0;
    s.FrameBorderSize = 0;
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kMuted;
    c[ImGuiCol_WindowBg] = kBg;
    c[ImGuiCol_ChildBg] = kPanel;
    c[ImGuiCol_PopupBg] = kPanel2;
    c[ImGuiCol_Border] = kBorder;
    c[ImGuiCol_FrameBg] = kPanel2;
    c[ImGuiCol_FrameBgHovered] = rgb(36, 41, 58);
    c[ImGuiCol_FrameBgActive] = rgb(42, 48, 68);
    c[ImGuiCol_Button] = rgb(36, 41, 58);
    c[ImGuiCol_ButtonHovered] = rgb(48, 54, 78);
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_Header] = rgb(124, 92, 255, 0.30f);
    c[ImGuiCol_HeaderHovered] = rgb(124, 92, 255, 0.50f);
    c[ImGuiCol_HeaderActive] = rgb(124, 92, 255, 0.70f);
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kAccentHi;
    c[ImGuiCol_CheckMark] = kAccentHi;
    c[ImGuiCol_ScrollbarBg] = rgb(0, 0, 0, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = rgb(50, 56, 80);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(70, 78, 108);
    c[ImGuiCol_ScrollbarGrabActive] = kAccent;
    c[ImGuiCol_Separator] = kBorder;
    c[ImGuiCol_TextSelectedBg] = rgb(124, 92, 255, 0.40f);
}

// ---------------------------------------------------------------- logo (PNG from memory via WIC)
bool load_logo(ID3D11Device* dev, ID3D11ShaderResourceView** out) {
    // S_OK, S_FALSE and RPC_E_CHANGED_MODE all mean COM is usable on this thread.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return false;
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(kLogoPng), static_cast<DWORD>(kLogoPngSize))))
        return false;
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
        return false;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(factory->CreateFormatConverter(&conv))) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                WICBitmapPaletteTypeCustom)))
        return false;
    UINT w = 0, h = 0;
    if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0 || w > 2048 || h > 2048) return false;
    std::vector<BYTE> px(static_cast<size_t>(w) * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(px.size()), px.data()))) return false;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = w;
    td.Height = h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{};
    sd.pSysMem = px.data();
    sd.SysMemPitch = w * 4;
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(dev->CreateTexture2D(&td, &sd, &tex))) return false;
    return SUCCEEDED(dev->CreateShaderResourceView(tex.Get(), nullptr, out));
}

// ---------------------------------------------------------------- small widgets
void muted(const char* t) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
    ImGui::TextUnformatted(t);
    ImGui::PopStyleColor();
}

void dot(const ImVec4& col) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = 5.0f;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f), r,
                                                ImGui::GetColorU32(col));
    ImGui::Dummy(ImVec2(r * 2 + 8, ImGui::GetTextLineHeight()));
    ImGui::SameLine(0, 0);
}

void status_row(bool ok, const char* name, const std::string& detail, bool warn_instead_of_bad = false) {
    dot(ok ? kOk : (warn_instead_of_bad ? kWarn : kBad));
    ImGui::TextUnformatted(name);
    if (!detail.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
        ImGui::TextWrapped("%s", detail.c_str());
        ImGui::PopStyleColor();
    }
}

void page_title(const char* t, const char* sub) {
    ImGui::SetWindowFontScale(1.45f);
    ImGui::TextUnformatted(t);
    ImGui::SetWindowFontScale(1.0f);
    muted(sub);
    ImGui::Spacing();
}

bool nav_button(const char* label, int idx) {
    const bool active = g.page == idx;
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, active ? rgb(124, 92, 255, 0.22f) : rgb(0, 0, 0, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(124, 92, 255, 0.32f));
    ImGui::PushStyleColor(ImGuiCol_Text, active ? kAccentHi : kText);
    const bool clicked = ImGui::Button(label, ImVec2(172, 42));
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return clicked;
}

// ---------------------------------------------------------------- data helpers
const char* skip_text(int s) {
    switch (static_cast<game::Skip>(s)) {
        case game::Skip::None: return "none (last frame ran)";
        case game::Skip::NoProfile: return "profile is empty";
        case game::Skip::NoPawn: return "no local pawn (not in a match?)";
        case game::Skip::NoController: return "local controller / SteamID unreadable";
        case game::Skip::NoServices: return "weapon services pointer unreadable";
        case game::Skip::BadVector: return "weapon list failed validation";
        case game::Skip::NoEntityList: return "entity list unreadable";
    }
    return "?";
}

std::string weapon_label(int def) {
    for (const auto& w : app().db.weapons)
        if (w.def_index == def) return w.label.empty() ? w.name : w.label;
    return "Weapon #" + std::to_string(def);
}

void rebuild_kit_view() {
    g.kit_view.clear();
    const std::string f = lower(g.kit_filter);
    const auto& kits = app().db.kits;
    for (size_t i = 0; i < kits.size(); ++i) {
        if (f.empty() || contains_ci(kits[i].label, f) || contains_ci(kits[i].name, f) ||
            std::to_string(kits[i].id).find(f) != std::string::npos)
            g.kit_view.push_back(static_cast<int>(i));
    }
    g.kit_view_filter = g.kit_filter;
    g.kit_view_valid = true;
}

// ---------------------------------------------------------------- pages
void page_skins() {
    page_title("Skins", "Pick a weapon, pick a finish. Changes are applied live when the game gates are green.");
    if (!app().skins.report().apply_ready() || !app().fsn_hook_ok.load()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
        ImGui::TextWrapped("Skin application is gated. Open the Status page to see what is missing. Your settings are still saved.");
        ImGui::PopStyleColor();
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float left_w = 270.0f;

    // ---- weapon list
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 14));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::BeginChild("##weapons", ImVec2(left_w, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##wf", "Search weapons...", g.weapon_filter, sizeof g.weapon_filter);

    struct Row {
        int def;
        std::string label;
    };
    std::vector<Row> rows;
    for (const auto& w : app().db.weapons) rows.push_back({w.def_index, w.label.empty() ? w.name : w.label});
    for (const auto& kv : app().profile) {
        bool known = false;
        for (const auto& r : rows) known = known || r.def == kv.first;
        if (!known) rows.push_back({kv.first, "Weapon #" + std::to_string(kv.first)});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.label < b.label; });

    ImGui::BeginChild("##wlist", ImVec2(0, avail.y - 120), ImGuiChildFlags_None);
    const std::string wf = lower(g.weapon_filter);
    for (const auto& r : rows) {
        if (!wf.empty() && !contains_ci(r.label, wf) && std::to_string(r.def).find(wf) == std::string::npos) continue;
        ImGui::PushID(r.def);
        if (ImGui::Selectable(r.label.c_str(), g.selected_def == r.def, 0, ImVec2(0, 30))) g.selected_def = r.def;
        auto it = app().profile.find(static_cast<uint16_t>(r.def));
        if (it != app().profile.end() && core::is_applicable(it->second)) {
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(mx.x - 12, (mn.y + mx.y) * 0.5f), 4.0f,
                                                        ImGui::GetColorU32(kOk));
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    muted("Add by weapon def index");
    ImGui::SetNextItemWidth(left_w - 28 - 52);
    ImGui::InputInt("##adddef", &g.add_def, 0, 0);
    ImGui::SameLine();
    if (ImGui::Button("Add", ImVec2(46, 0)) && g.add_def > 0 && g.add_def <= 0xFFFF) {
        app().profile.try_emplace(static_cast<uint16_t>(g.add_def));
        g.selected_def = g.add_def;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::SameLine();

    // ---- editor
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::BeginChild("##editor", ImVec2(0, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (g.selected_def == 0) {
        muted("Select a weapon on the left.");
    } else {
        const uint16_t def = static_cast<uint16_t>(g.selected_def);
        auto found = app().profile.find(def);
        core::SkinEntry cur = found != app().profile.end() ? found->second : core::SkinEntry{};
        bool dirty = false;

        ImGui::SetWindowFontScale(1.25f);
        ImGui::TextUnformatted(weapon_label(g.selected_def).c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine();
        muted(("def " + std::to_string(g.selected_def)).c_str());

        dirty |= ImGui::Checkbox("Enable skin for this weapon", &cur.enabled);

        ImGui::Separator();
        const auto& kits = app().db.kits;
        std::string selected_name = "none";
        for (const auto& k : kits)
            if (k.id == cur.paint_kit) selected_name = k.label.empty() ? k.name : k.label;
        ImGui::TextUnformatted("Finish");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kAccentHi);
        ImGui::Text("%s  (id %u)", selected_name.c_str(), cur.paint_kit);
        ImGui::PopStyleColor();

        if (kits.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kWarn);
            ImGui::TextWrapped("skins.tsv is empty or missing (%s). Generate it with tools/gen_skins.py, or type a paint kit id below.",
                               app().db_note.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##kf", "Search finishes by name or id...", g.kit_filter, sizeof g.kit_filter);
            if (!g.kit_view_valid || g.kit_view_filter != g.kit_filter) rebuild_kit_view();
            ImGui::BeginChild("##kits", ImVec2(0, 200), ImGuiChildFlags_None);
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(g.kit_view.size()), 26.0f);
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const auto& k = kits[static_cast<size_t>(g.kit_view[static_cast<size_t>(i)])];
                    std::string text = (k.label.empty() ? k.name : k.label) + "   #" + std::to_string(k.id);
                    ImGui::PushID(i);
                    if (ImGui::Selectable(text.c_str(), cur.paint_kit == k.id, 0, ImVec2(0, 24))) {
                        cur.paint_kit = k.id;
                        dirty = true;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }

        int pk = static_cast<int>(cur.paint_kit);
        ImGui::SetNextItemWidth(160);
        if (ImGui::InputInt("Paint kit ID", &pk, 0, 0)) {
            cur.paint_kit = pk < 0 ? 0u : static_cast<uint32_t>(pk);
            dirty = true;
        }

        int seed = static_cast<int>(cur.seed);
        ImGui::SetNextItemWidth(260);
        if (ImGui::SliderInt("Pattern seed", &seed, 0, static_cast<int>(core::kMaxSeed))) {
            cur.seed = static_cast<uint32_t>(seed);
            dirty = true;
        }

        ImGui::SetNextItemWidth(260);
        dirty |= ImGui::SliderFloat("Wear", &cur.wear, 0.0f, 1.0f, "%.4f");
        static const struct { const char* name; float v; } presets[] = {
            {"FN", 0.03f}, {"MW", 0.11f}, {"FT", 0.26f}, {"WW", 0.41f}, {"BS", 0.70f}};
        for (size_t i = 0; i < 5; ++i) {
            if (i) ImGui::SameLine();
            if (ImGui::Button(presets[i].name, ImVec2(46, 0))) {
                cur.wear = presets[i].v;
                dirty = true;
            }
        }

        bool st = cur.stattrak >= 0;
        if (ImGui::Checkbox("StatTrak counter", &st)) {
            cur.stattrak = st ? 0 : -1;
            dirty = true;
        }
        if (st) {
            int count = cur.stattrak;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140);
            if (ImGui::InputInt("##stc", &count, 0, 0)) {
                cur.stattrak = std::clamp(count, 0, core::kMaxStatTrak);
                dirty = true;
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Save profile", ImVec2(130, 0))) app().save_profile();
        ImGui::SameLine();
        if (ImGui::Button("Reset weapon", ImVec2(130, 0))) {
            app().profile.erase(def);
            app().publish_profile();
            dirty = false;
        }

        if (dirty) {
            app().profile[def] = core::sanitize(cur);
            app().publish_profile();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

std::string join(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
    return out;
}

void page_status() {
    page_title("Status", "Every gate that must be green before a single byte is written to the game.");
    const game::Report& r = app().skins.report();
    const game::Counters& c = app().skins.counters();

    status_row(r.config_file_found, "offsets.ini found", r.config_file_found ? "" : "put offsets.ini next to the DLL");
    status_row(r.missing_keys.empty() && r.bad_values.empty(), "Required keys present and parsable",
               r.missing_keys.empty() ? (r.bad_values.empty() ? "" : "bad: " + join(r.bad_values))
                                      : "missing: " + join(r.missing_keys));
    status_row(r.layout_ok, "Layout consistent", r.layout_error);
    status_row(r.client_found, "client.dll loaded", "");
    status_row(r.build_ok, "Build identity matches offsets.ini", r.build_detail);
    status_row(app().present_hook_ok.load(), "Present / ResizeBuffers hooks", app().hook_error);
    status_row(r.fsn_resolved, "FrameStageNotify signature", r.fsn_detail);
    status_row(app().fsn_hook_ok.load(), "FrameStageNotify hook active",
               app().fsn_hook_ok.load() ? "" : "needs: unique signature, frame_stage_apply, confirm_fsn_prototype=1");
    status_row(app().skins.regen_active(), "Skin regenerate function",
               r.regen_detail + (app().skins.regen_active() ? "" : "  (optional; without it a skin may need a weapon re-equip to show)"),
               true);
    if (!r.notes.empty()) status_row(false, "Notes", r.notes, true);

    ImGui::Separator();
    ImGui::Text("Frames processed: %llu    Weapons seen: %llu    Modified: %llu",
                static_cast<unsigned long long>(c.frames.load()), static_cast<unsigned long long>(c.weapons_seen.load()),
                static_cast<unsigned long long>(c.weapons_modified.load()));
    ImGui::Text("Write failures: %llu    Stale handles skipped: %llu    Regenerate calls: %llu",
                static_cast<unsigned long long>(c.write_failures.load()),
                static_cast<unsigned long long>(c.stale_handles.load()),
                static_cast<unsigned long long>(c.regen_calls.load()));
    muted(("Last skip reason: " + std::string(skip_text(c.last_skip.load()))).c_str());
    muted(("skins.tsv: " + app().db_note).c_str());
}

void page_settings() {
    page_title("Settings", "General options.");
    bool on = app().skins.enabled();
    if (ImGui::Checkbox("Apply skins (master switch)", &on)) app().skins.set_enabled(on);
    muted("Menu key: INSERT");
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, rgb(120, 40, 50));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(160, 55, 65));
    if (ImGui::Button("Disable and unhook", ImVec2(200, 0))) g.request_unload = true;
    ImGui::PopStyleColor(2);
    muted("Hooks are turned into pass-throughs. The DLL stays loaded on purpose: a game thread may still be inside it.");
}

// ---------------------------------------------------------------- main window
void draw_sidebar(float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanel);
    ImGui::BeginChild("##side", ImVec2(210, height), ImGuiChildFlags_None);
    ImGui::SetCursorPos(ImVec2(20, 22));
    if (g.logo) {
        ImGui::Image((ImTextureID)g.logo.Get(), ImVec2(44, 44));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextUnformatted("Skin Changer");
    ImGui::SetWindowFontScale(1.0f);
    muted("Counter-Strike 2");
    ImGui::EndGroup();

    ImGui::SetCursorPos(ImVec2(19, 96));
    if (nav_button("  Skins", 0)) g.page = 0;
    ImGui::SetCursorPosX(19);
    if (nav_button("  Status", 1)) g.page = 1;
    ImGui::SetCursorPosX(19);
    if (nav_button("  Settings", 2)) g.page = 2;

    const bool live = app().skins.report().apply_ready() && app().fsn_hook_ok.load() && app().skins.enabled();
    ImGui::SetCursorPos(ImVec2(22, height - 46));
    dot(live ? kOk : kWarn);
    ImGui::PushStyleColor(ImGuiCol_Text, live ? kOk : kWarn);
    ImGui::TextUnformatted(live ? "Applying" : "Gated / idle");
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void draw_main() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(900, 580), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 0.5f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("##skinchanger", nullptr, flags)) {
        ImGui::End();
        return;
    }
    const float height = ImGui::GetWindowSize().y;
    draw_sidebar(height);
    ImGui::SameLine(0, 0);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(0, 0, 0, 0.0f));
    ImGui::BeginChild("##content", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (g.page) {
        case 0: page_skins(); break;
        case 1: page_status(); break;
        default: page_settings(); break;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::End();
}

// ---------------------------------------------------------------- window procedure
LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (app().active.load()) {
        if (m == WM_KEYDOWN && w == VK_INSERT && !(l & (1 << 30))) app().menu_visible.store(!app().menu_visible.load());
        if (app().menu_visible.load()) {
            ImGui_ImplWin32_WndProcHandler(h, m, w, l);
            switch (m) {
                case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
                case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
                case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
                case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
                case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
                case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR:
                    return 0;  // swallowed while the menu is open
                default: break;
            }
        }
    }
    return CallWindowProcW(g.orig_wndproc, h, m, w, l);
}

bool init(IDXGISwapChain* sc) {
    if (g.failed) return false;
    auto fail = [&](const char* why) {
        g.failed = true;
        util::log("ui init failed: %s", why);
        return false;
    };
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc)) || !desc.OutputWindow) return fail("swap chain desc / window");
    if (FAILED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(g.dev.GetAddressOf()))))
        return fail("swap chain is not a D3D11 one (is the game running Vulkan?)");
    g.dev->GetImmediateContext(&g.ctx);
    g.hwnd = desc.OutputWindow;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    const char* font_path = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (GetFileAttributesA(font_path) != INVALID_FILE_ATTRIBUTES) io.Fonts->AddFontFromFileTTF(font_path, 17.0f);
    apply_theme();
    if (!ImGui_ImplWin32_Init(g.hwnd)) return fail("ImGui_ImplWin32_Init");
    if (!ImGui_ImplDX11_Init(g.dev.Get(), g.ctx.Get())) return fail("ImGui_ImplDX11_Init");
    if (!load_logo(g.dev.Get(), g.logo.GetAddressOf())) util::log("logo failed to load (UI continues without it)");

    g.orig_wndproc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wnd_proc)));
    g.inited = true;
    util::log("ui initialised");
    return true;
}

void create_rtv(IDXGISwapChain* sc) {
    ComPtr<ID3D11Texture2D> back;
    if (SUCCEEDED(sc->GetBuffer(0, IID_PPV_ARGS(&back)))) g.dev->CreateRenderTargetView(back.Get(), nullptr, &g.rtv);
}

}  // namespace

namespace ui {

void on_present(IDXGISwapChain* sc) {
    if (!g.inited && !init(sc)) return;

    const bool visible = app().menu_visible.load();
    if (g.was_visible && !visible) app().save_profile();
    g.was_visible = visible;

    if (visible) {
        if (!g.rtv) create_rtv(sc);
        ImGui::GetIO().MouseDrawCursor = true;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        draw_main();
        ImGui::Render();
        if (g.rtv) {
            ID3D11RenderTargetView* rtv = g.rtv.Get();
            g.ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    }

    if (g.request_unload) {
        g.request_unload = false;
        hooks::deactivate();
    }
}

void on_resize_begin() { g.rtv.Reset(); }

void shutdown() {
    if (!g.inited || !g.hwnd || !g.orig_wndproc) return;
    // Restore only if nobody subclassed after us; otherwise stay in the chain as a pass-through.
    if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(g.hwnd, GWLP_WNDPROC)) == &wnd_proc)
        SetWindowLongPtrW(g.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g.orig_wndproc));
}

}  // namespace ui
