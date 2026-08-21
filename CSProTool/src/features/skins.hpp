#pragma once
#include <cstdint>

namespace skins {
    struct Option {
        const char* name;
        int paint;
    };

    struct Weapon {
        const char* name;
        uint16_t def;
        const Option* options;
        int option_count;
    };

    struct Knife {
        const char* name;
        uint16_t def;
        uint32_t subclass;
        const char* model;
    };

    constexpr int k_max_weapons = 16;

    const Weapon* catalog(int* count);
    const Knife* knife_catalog(int* count);
    int weapon_index(uint16_t def);
    int paint_for(uint16_t def);
    const char* current_weapon_name();
    void sync_menu_weapon();
    const char* debug_line();
    const char* knife_debug_line();
    void force_refresh();
    void on_world_lost();
    bool init_hooks();
    void shutdown_hooks();
    bool hooks_ready();
    void run(bool allow_regen);
}
