#include "features/trigger.hpp"
#include "features/config.hpp"
#include "features/ssg.hpp"
#include "features/target.hpp"
#include "sdk/entity.hpp"
#include "sdk/spread.hpp"

namespace features {
    static constexpr int k_plus = 65537;
    static constexpr int k_minus = 256;

    static int g_hold = 0;

    static void write_attack(bool down) {
        const auto client = game::client_base();
        if (!client) return;
        mem::write<int>(client + schema::btn::attack, down ? k_plus : k_minus);
    }

    static bool is_gun(uint16_t def) {
        if (def == 0) return false;
        if (def == 42 || def == 59 || def == 80) return false;
        if (def >= 500 && def < 600) return false;
        if (def >= 43 && def <= 49) return false;
        if (def == 31) return false;
        return true;
    }

    void run_triggerbot() {
        if (!cfg::combat::triggerbot || cfg::menu_open) {
            if (g_hold > 0) {
                if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
                    write_attack(false);
                g_hold = 0;
            }
            return;
        }
        if (features::ssg_want_spread())
            return;
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
            return;

        if (!game::world_ready() || !game::local_alive())
            return;

        const auto local = game::local_pawn();
        if (!mem::valid(local) || mem::read<int>(local + schema::C_BaseEntity::m_iHealth) <= 0)
            return;
        const auto wpn = game::active_weapon(local);
        if (!mem::valid(wpn) || !is_gun(spread::item_index(wpn)))
            return;

        int sw = silent::screen_w.load(std::memory_order_relaxed);
        int sh = silent::screen_h.load(std::memory_order_relaxed);
        if (sw < 16) sw = 1920;
        if (sh < 16) sh = 1080;
        const Vec2 center{ sw * 0.5f, sh * 0.5f };
        float fov = cfg::combat::trigger_fov;
        if (fov < 2.f) fov = 2.f;
        if (fov > 24.f) fov = 24.f;

        const auto vm = game::view_matrix();
        bool hit = false;
        for (const auto& p : game::collect_players(!cfg::combat::team_check)) {
            if (cfg::combat::team_check && p.teammate && game::player_id(p) != cfg::combat::focus_id) continue;
            if (cfg::combat::focus_id && game::player_id(p) != cfg::combat::focus_id) continue;
            if (!game::wall_ok(p.pawn)) continue;
            Vec3 pt = game::aim_pos(p.pawn, cfg::combat::aim_body);
            if (pt.length() < 1.f) pt = p.head;
            Vec2 s{};
            if (!vm.world_to_screen(pt, s, sw, sh)) continue;
            const float d = Vec2{ s.x - center.x, s.y - center.y }.length();
            if (d <= fov) {
                hit = true;
                break;
            }
        }

        if (hit) {
            write_attack(true);
            g_hold = 3;
            return;
        }
        if (g_hold > 0) {
            --g_hold;
            write_attack(true);
            return;
        }
        write_attack(false);
    }
}
