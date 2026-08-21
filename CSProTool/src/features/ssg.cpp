#include "features/ssg.hpp"
#include "features/config.hpp"
#include "features/ragebot.hpp"
#include "features/target.hpp"
#include "sdk/entity.hpp"
#include "sdk/spread.hpp"
#include "hooks/createmove.hpp"
#include "imgui.h"
#include <cmath>
#include <cstdio>

namespace features {
    static constexpr int k_plus = 65537;
    static constexpr int k_minus = 256;
    static constexpr uint16_t k_ssg = 40;
    static constexpr float k_stop_speed = 165.f;
    static constexpr float k_max_inacc = 0.058f;
    static constexpr float k_hard_spd = 210.f;
    static constexpr int k_apex_hold = 1; // apex exact 1 tick

    static bool g_hold_attack = false;
    static DWORD g_hold_until = 0;
    static bool g_rose = false;
    static bool g_fired = false;
    static int g_apex_ticks = 0;
    static bool g_armed = false;
    static bool g_block_jump = false;
    static bool g_stop = false;
    static bool g_want_spread = false;
    static Vec3 g_wish{};
    static DWORD g_last_shot = 0;
    static float g_last_vz = 0.f;
    static float g_shot_vz = 0.f;
    static float g_shot_inacc = 0.f;
    static char g_dbg[96] = "SSG jump";

    static bool lmb_down() {
        return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    }

    static bool space_down() {
        return (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    }

    static void write_attack(bool down) {
        const auto client = game::client_base();
        if (!client) return;
        mem::write<int>(client + schema::btn::attack, down ? k_plus : k_minus);
    }

    static bool holding_ssg(uintptr_t wpn) {
        if (!mem::valid(wpn)) return false;
        if (spread::item_index(wpn) == k_ssg)
            return true;
        char dn[72]{};
        return game::designer_name(wpn, dn, sizeof(dn)) && std::strstr(dn, "ssg08");
    }

    static int game_tick() {
        const auto gv = mem::read<uintptr_t>(game::client_base() + schema::off::client_dll::dwGlobalVars);
        if (!mem::valid(gv)) return 0;
        const int a = mem::read<int>(gv + 0x44);
        if (a > 100 && a < 200000000) return a;
        const int b = mem::read<int>(gv + 0x48);
        if (b > 100 && b < 200000000) return b;
        return 0;
    }

    static bool weapon_ready(uintptr_t wpn, DWORD now) {
        if (mem::read<int>(wpn + schema::C_BasePlayerWeapon::m_iClip1) <= 0)
            return false;
        if (mem::read<uint8_t>(wpn + schema::C_CSWeaponBase::m_bInReload))
            return false;
        const int next = mem::read<int>(wpn + schema::C_BasePlayerWeapon::m_nNextPrimaryAttackTick);
        const int tick = game_tick();
        if (tick > 0 && next > tick + 1)
            return false;
        if (g_last_shot && now - g_last_shot < 1100)
            return false;
        return true;
    }

    static void reset_air() {
        g_rose = false;
        g_fired = false;
        g_apex_ticks = 0;
        g_armed = false;
    }

    static const char* timing_label(float vz) {
        if (vz > 8.f) return "TROP TOT";
        if (vz < -8.f) return "TROP TARD";
        return "APEX";
    }

    static bool precise_enough(float inacc, float spd) {
        if (inacc <= k_max_inacc)
            return true;
        if (spd <= 130.f && inacc <= k_max_inacc + 0.012f)
            return true;
        return false;
    }

    static void fire_now(const Vec3& head, float vz, float inacc, DWORD now) {
        g_shot_vz = vz;
        g_shot_inacc = inacc;
        g_wish = head;
        write_attack(true);
        g_hold_attack = true;
        g_hold_until = now + 80;
        g_want_spread = true;
        g_fired = true;
        g_last_shot = now;
        g_block_jump = false;
        g_stop = false;
        g_apex_ticks = 0;
        std::snprintf(g_dbg, sizeof(g_dbg), "SSG  %s  TETE  vz %.0f  inacc %.3f",
            timing_label(vz), vz, inacc);
    }

    static void apply_ssg() {
        g_block_jump = false;
        g_stop = false;

        if (!cfg::combat::ssg_jump || cfg::menu_open) {
            if (g_hold_attack && !lmb_down())
                write_attack(false);
            g_hold_attack = false;
            g_hold_until = 0;
            g_want_spread = false;
            reset_air();
            std::snprintf(g_dbg, sizeof(g_dbg), "SSG jump");
            return;
        }

        const auto local = game::local_pawn();
        if (!mem::valid(local) || mem::read<int>(local + schema::C_BaseEntity::m_iHealth) <= 0) {
            g_hold_attack = false;
            g_hold_until = 0;
            g_want_spread = false;
            reset_air();
            return;
        }

        const auto wpn = game::active_weapon(local);
        if (!holding_ssg(wpn)) {
            if (g_hold_attack && !lmb_down())
                write_attack(false);
            g_hold_attack = false;
            g_hold_until = 0;
            g_want_spread = false;
            reset_air();
            std::snprintf(g_dbg, sizeof(g_dbg), "SSG jump : prends le SSG");
            return;
        }

        const DWORD now = GetTickCount();
        const auto vel = mem::read<Vec3>(local + schema::C_BaseEntity::m_vecAbsVelocity);
        const float vz = vel.z;
        const float spd = vel.length2d();
        const float inacc = spread::live_inaccuracy(local, wpn);

        if (g_hold_attack) {
            if (now < g_hold_until) {
                write_attack(true);
                g_last_vz = vz;
                return;
            }
            if (!lmb_down())
                write_attack(false);
            g_hold_attack = false;
            g_want_spread = false;
        }

        const int flags = mem::read<int>(local + schema::C_BaseEntity::m_fFlags);
        const uint32_t ground_h = mem::read<uint32_t>(local + schema::C_BaseEntity::m_hGroundEntity);
        const bool has_ground = ground_h != 0 && ground_h != 0xFFFFFFFFu;
        const bool on_ground = (flags & 1) != 0 && has_ground && std::fabs(vz) < 45.f;
        const bool takeoff = (g_last_vz < 90.f && vz > 110.f)
            || (g_last_vz < 0.f && vz > 140.f);
        const bool landed = on_ground && g_last_vz < -35.f && !takeoff;

        if (takeoff) {
            g_armed = true;
            g_rose = true;
            g_apex_ticks = 0;
            g_fired = false;
        } else if (landed) {
            reset_air();
        }

        if (vz > 35.f)
            g_rose = true;

        const bool ready = weapon_ready(wpn, now);
        Vec3 head{};
        const bool got_head = target::find_ssg_head(head);
        const bool want_jump = space_down();

        if (on_ground && want_jump && ready && got_head && spd > k_stop_speed) {
            g_block_jump = true;
            g_stop = true;
            g_last_vz = vz;
            std::snprintf(g_dbg, sizeof(g_dbg), "SSG  stop  spd %.0f  inacc %.3f", spd, inacc);
            return;
        }

        if (g_rose) {
            std::snprintf(g_dbg, sizeof(g_dbg), "SSG  %s  vz %.0f  inacc %.3f  spd %.0f%s",
                g_fired ? timing_label(g_shot_vz) : (g_apex_ticks ? "apex" : "monte"),
                g_fired ? g_shot_vz : vz, inacc, spd,
                ready ? "" : "  wait");
        } else if (ready) {
            std::snprintf(g_dbg, sizeof(g_dbg), "SSG  pret  espace + vise");
        } else {
            std::snprintf(g_dbg, sizeof(g_dbg), "SSG  bolt wait");
        }

        const int clip = mem::read<int>(wpn + schema::C_BasePlayerWeapon::m_iClip1);
        if (clip == 0) {
            g_last_vz = vz;
            return;
        }

        // Apex parfait : croisement vz+/vz- UNIQUEMENT (+ filet |vz|<=6).
        float step = g_last_vz - vz;
        if (step < 1.f || step > 40.f)
            step = 12.5f;
        const float vz_next = vz - step;
        const bool crossed = g_last_vz > 0.f && vz <= 0.f;
        const bool this_best = g_rose && g_last_vz > 0.f
            && vz >= 0.f && vz <= 6.f
            && std::fabs(vz) <= std::fabs(vz_next);
        const bool at_apex = crossed || this_best;
        if (g_rose && g_armed && at_apex)
            g_apex_ticks = k_apex_hold;
        else if (g_apex_ticks > 0)
            --g_apex_ticks;

        if (!g_fired && vz < -20.f)
            g_fired = true;

        g_last_vz = vz;

        if (!g_armed || g_fired || !ready)
            return;
        if (!got_head)
            return;
        if (g_apex_ticks <= 0)
            return;
        if (spd > k_hard_spd && !precise_enough(inacc, spd)) {
            std::snprintf(g_dbg, sizeof(g_dbg), "SSG  SKIP hop spd %.0f", spd);
            return;
        }
        if (!precise_enough(inacc, spd)) {
            std::snprintf(g_dbg, sizeof(g_dbg), "SSG  SKIP inacc %.3f", inacc);
            return;
        }

        fire_now(head, vz, inacc, now);
    }

    void ssg_flush_attack() {
        if (!g_hold_attack)
            return;
        if (GetTickCount() < g_hold_until)
            write_attack(true);
    }

    bool ssg_want_spread() {
        return g_want_spread && g_hold_attack && GetTickCount() < g_hold_until;
    }

    bool ssg_force_stop() {
        return cfg::combat::ssg_jump && !cfg::menu_open && g_block_jump;
    }

    bool ssg_block_jump() {
        return cfg::combat::ssg_jump && !cfg::menu_open && g_block_jump;
    }

    bool ssg_wish_ang(Vec3& out) {
        out = g_wish;
        return ssg_want_spread();
    }

    bool ssg_use_silent() {
        // TOUJOURS silent : ne jamais voler la camera (sinon impossible de viser).
        return cfg::combat::ssg_jump;
    }

    void ssg_mark_head(bool hit) {
        if (!g_want_spread)
            return;
        std::snprintf(g_dbg, sizeof(g_dbg), "SSG  %s  %s  vz %.0f  inacc %.3f",
            timing_label(g_shot_vz), hit ? "TETE" : "vise", g_shot_vz, g_shot_inacc);
    }

    void ssg_abort_shot(const char* why) {
        if (g_hold_attack && !lmb_down())
            write_attack(false);
        g_hold_attack = false;
        g_hold_until = 0;
        g_want_spread = false;
        std::snprintf(g_dbg, sizeof(g_dbg), "SSG  SKIP %s", why ? why : "cone");
    }

    void run_ssg_tick() {
        // Ragebot possede le laser SSG (apex + micro-stop) : pas de double owner.
        if (ragebot_on()) {
            if (g_hold_attack && !lmb_down())
                write_attack(false);
            g_hold_attack = false;
            g_want_spread = false;
            g_block_jump = false;
            g_stop = false;
            return;
        }
        apply_ssg();
    }

    void run_ssg() {
        if (hooks::createmove_active()) {
            ssg_flush_attack();
            return;
        }
        apply_ssg();
    }

    void render_ssg_hud(int, int screen_h) {
        if (!cfg::combat::ssg_jump || cfg::menu_open)
            return;
        auto* dl = ImGui::GetForegroundDrawList();
        if (!dl) return;
        const ImU32 col = (std::strstr(g_dbg, "TETE"))
            ? IM_COL32(80, 255, 160, 255)
            : (std::strstr(g_dbg, "APEX") || std::strstr(g_dbg, "apex") || std::strstr(g_dbg, "pret"))
                ? IM_COL32(80, 220, 140, 255)
                : (std::strstr(g_dbg, "SKIP") || std::strstr(g_dbg, "TROP") || std::strstr(g_dbg, "bolt")
                    || std::strstr(g_dbg, "stop") || std::strstr(g_dbg, "hop")
                    ? IM_COL32(255, 170, 60, 255)
                    : IM_COL32(230, 230, 230, 220));
        dl->AddText(ImVec2(18.f, screen_h * 0.58f), col, g_dbg);
    }
}
