#include "hooks/hooks.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <vector>

#include "MinHook.h"
#include "app.hpp"
#include "ui/ui.hpp"
#include "util/log.hpp"

using Microsoft::WRL::ComPtr;

namespace {

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
// Prototype void(void* this, int stage) is only used after the user set confirm_fsn_prototype=1.
using FsnFn = void(__fastcall*)(void*, int);

PresentFn g_present = nullptr;
ResizeFn g_resize = nullptr;
FsnFn g_fsn = nullptr;

HRESULT WINAPI hk_present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (app().active.load()) {
        try {
            ui::on_present(sc);
        } catch (...) {
            util::log("exception in ui::on_present");
        }
    }
    return g_present(sc, sync, flags);
}

HRESULT WINAPI hk_resize(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    if (app().active.load()) ui::on_resize_begin();  // must drop every reference to the back buffer
    return g_resize(sc, count, w, h, fmt, flags);
}

void __fastcall hk_fsn(void* self, int stage) {
    const bool on = app().active.load();
    if (on) {
        try { app().skins.on_frame_stage(stage, false); } catch (...) { util::log("exception in skins (before)"); }
    }
    g_fsn(self, stage);
    if (on) {
        try { app().skins.on_frame_stage(stage, true); } catch (...) { util::log("exception in skins (after)"); }
    }
}

// Reads Present / ResizeBuffers out of a throw-away swap chain. IDXGISwapChain
// vtable slots 8 (Present) and 13 (ResizeBuffers) come from the documented COM
// interface order.
bool get_swapchain_methods(void*& present, void*& resize, std::string& err) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"SkinChangerDummyWnd";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr,
                                wc.hInstance, nullptr);
    bool ok = false;
    if (!hwnd) {
        err = "dummy window creation failed";
    } else {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        ComPtr<IDXGISwapChain> sc;
        ComPtr<ID3D11Device> dev;
        ComPtr<ID3D11DeviceContext> ctx;
        D3D_FEATURE_LEVEL fl{};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                                   D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);
        if (FAILED(hr)) {
            err = "D3D11CreateDeviceAndSwapChain failed, hr=" + std::to_string(static_cast<long>(hr));
        } else {
            void** vt = *reinterpret_cast<void***>(sc.Get());
            present = vt[8];
            resize = vt[13];
            ok = present && resize;
            if (!ok) err = "swap chain vtable entries are null";
        }
        DestroyWindow(hwnd);
    }
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

}  // namespace

namespace hooks {

bool install(std::string& error) {
    void* present = nullptr;
    void* resize = nullptr;
    if (!get_swapchain_methods(present, resize, error)) return false;

    if (MH_Initialize() != MH_OK) {
        error = "MH_Initialize failed";
        return false;
    }
    std::vector<void*> created;
    auto rollback = [&](const char* what) {
        MH_DisableHook(MH_ALL_HOOKS);
        for (void* t : created) MH_RemoveHook(t);
        MH_Uninitialize();  // nothing was enabled yet, so no thread can be inside a trampoline
        error = what;
        return false;
    };

    if (MH_CreateHook(present, reinterpret_cast<void*>(&hk_present), reinterpret_cast<void**>(&g_present)) != MH_OK)
        return rollback("MH_CreateHook(Present) failed");
    created.push_back(present);
    if (MH_CreateHook(resize, reinterpret_cast<void*>(&hk_resize), reinterpret_cast<void**>(&g_resize)) != MH_OK)
        return rollback("MH_CreateHook(ResizeBuffers) failed");
    created.push_back(resize);

    const bool want_fsn = app().skins.fsn_hookable();
    if (want_fsn) {
        void* target = reinterpret_cast<void*>(app().skins.fsn_address());
        if (MH_CreateHook(target, reinterpret_cast<void*>(&hk_fsn), reinterpret_cast<void**>(&g_fsn)) != MH_OK)
            return rollback("MH_CreateHook(FrameStageNotify) failed");
        created.push_back(target);
    }

    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) return rollback("MH_EnableHook failed");
    app().present_hook_ok.store(true);
    app().fsn_hook_ok.store(want_fsn);
    util::log("hooks installed: present=1 fsn=%d", want_fsn);
    return true;
}

void deactivate() {
    app().active.store(false);
    MH_DisableHook(MH_ALL_HOOKS);
    ui::shutdown();
    util::log("hooks deactivated (module stays loaded)");
}

}  // namespace hooks
