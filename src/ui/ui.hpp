#pragma once
#include <dxgi.h>

namespace ui {

// Render thread, called from the Present detour.
void on_present(IDXGISwapChain* swap_chain);
// Called from the ResizeBuffers detour before the original runs.
void on_resize_begin();
// Restores the window procedure if it is still ours. ImGui/D3D objects are
// intentionally left allocated (hooks may still be mid-flight).
void shutdown();

}  // namespace ui
