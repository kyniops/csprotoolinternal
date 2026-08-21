#pragma once
#include <cstdint>
namespace cham_draw {
    void publish(const uintptr_t* allow, int nallow, const uintptr_t* deny, int ndeny,
                 bool on, unsigned char r, unsigned char g, unsigned char b);
    void publish_colored(const uintptr_t* ents, const uint8_t* rgb, int n, bool on);
    void ensure_hooks();
    bool mesh_glow_ready();
    void shutdown_hooks();
}
