#pragma once
#include <string>

namespace hooks {

// Creates every hook first, enables them together, and rolls everything back
// if any step fails. Returns false and fills `error` on failure.
bool install(std::string& error);

// Turns every detour into a pass-through and disables the hooks. The DLL stays
// loaded on purpose: a thread may still be inside a trampoline.
void deactivate();

}  // namespace hooks
