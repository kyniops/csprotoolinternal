#include "features/aimbot.hpp"
#include "features/target.hpp"
#include "hooks/createmove.hpp"
#include "menu/menu.hpp"

namespace features {
    static void edge_toggle(int vk, bool& flag) {
        if (vk <= 0 || vk > 255) return;
        static bool state[256]{};
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const int i = vk & 0xFF;
        if (down && !state[i]) flag = !flag;
        state[i] = down;
    }

    void poll_feature_hotkeys() {
        if (menu::poll_bind_capture())
            return;
        edge_toggle(cfg::visuals::esp_key, cfg::visuals::esp);
        edge_toggle(cfg::combat::silent_key, cfg::combat::silent);
        edge_toggle(cfg::visuals::third_person_key, cfg::visuals::third_person);
    }

    // Backup Present : seulement si CreateMove n'est pas encore accroche.
    void run_aimbot(int screen_w, int screen_h) {
        poll_feature_hotkeys();
        silent::set_screen(screen_w, screen_h);

        if (hooks::createmove_active())
            return;

        if (cfg::combat::silent)
            return;

        const bool want_snap = cfg::combat::aimbot || cfg::combat::rage;
        if (!want_snap)
            return;

        Vec3 angles{};
        if (!target::find_head(angles, nullptr, cfg::combat::rage))
            return;

        game::set_view_angles(angles);
    }
}
