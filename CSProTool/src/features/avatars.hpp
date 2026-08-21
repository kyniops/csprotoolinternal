#pragma once
#include "imgui.h"
#include <cstdint>

namespace avatars {
    void tick();
    void request(uint64_t steamid);
    bool get(uint64_t steamid, ImTextureID* tex, int* w, int* h);
    void shutdown();
}
