#include "features/hitmarker.hpp"
#include "features/config.hpp"
#include "sdk/entity.hpp"
#include "imgui.h"
#include <cstdio>
#include <cstring>

namespace features {
    static constexpr DWORD k_hit_ms = 420;
    static constexpr DWORD k_kill_ms = 720;
    static constexpr DWORD k_fire_ms = 420;

    static DWORD g_until = 0;
    static bool g_kill = false;
    static DWORD g_last_fire = 0;
    static int g_last_shots = -1;
    static int g_hp[65]{};
    static uintptr_t g_pawn[65]{};
    static bool g_have[65]{};
    static Vec3 g_hit_pos{};
    static bool g_hit_world = false;
    static int g_dmg = 0;

    void reset_hitmarker();

    static bool local_fired(uintptr_t local, DWORD now) {
        const int shots = mem::read<int>(local + schema::C_CSPlayerPawn::m_iShotsFired);
        if (shots >= 0 && shots < 80 && shots > g_last_shots && g_last_shots >= 0)
            g_last_fire = now;
        g_last_shots = shots;

        const auto client = game::client_base();
        if (client) {
            const int atk = mem::read<int>(client + schema::btn::attack);
            if (atk == 65537 || (atk & 1) != 0)
                g_last_fire = now;
        }
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
            g_last_fire = now;
        return g_last_fire && now - g_last_fire <= k_fire_ms;
    }

    static void trigger(bool kill, DWORD now, const Vec3& pos, int dmg) {
        g_kill = kill || g_kill;
        g_until = now + (g_kill ? k_kill_ms : k_hit_ms);
        g_hit_pos = pos;
        g_hit_world = pos.length() > 1.f;
        g_dmg = dmg > 0 ? dmg : g_dmg;
    }

    static void poll() {
        if (!cfg::visuals::hitmarker)
            return;
        const DWORD now = GetTickCount();
        if (g_until && now >= g_until) {
            g_until = 0;
            g_kill = false;
            g_hit_world = false;
            g_dmg = 0;
        }
        if (!game::world_ready()) {
            reset_hitmarker();
            return;
        }
        const auto local = game::local_pawn();
        if (!mem::valid(local) || !game::local_alive()) {
            std::memset(g_have, 0, sizeof(g_have));
            std::memset(g_pawn, 0, sizeof(g_pawn));
            g_last_shots = -1;
            return;
        }

        const bool fired = local_fired(local, now);
        const int local_team = mem::read<uint8_t>(local + schema::C_BaseEntity::m_iTeamNum);

        for (int i = 1; i <= 64; ++i) {
            const auto ctrl = game::get_entity(static_cast<uint32_t>(i));
            if (!mem::valid(ctrl)) {
                g_have[i] = false;
                continue;
            }
            const auto handle = mem::read<uint32_t>(ctrl + schema::CCSPlayerController::m_hPlayerPawn);
            if (!handle || handle == 0xFFFFFFFFu) {
                g_have[i] = false;
                continue;
            }
            const auto pawn = game::get_entity(handle);
            if (!mem::valid(pawn) || pawn == local) {
                g_have[i] = false;
                continue;
            }

            int hp = mem::read<int>(pawn + schema::C_BaseEntity::m_iHealth);
            const uint8_t life = mem::read<uint8_t>(pawn + schema::C_BaseEntity::m_lifeState);
            if (hp < 0) hp = 0;
            if (hp > 200) hp = 200;
            if (life != 0)
                hp = 0;

            if (g_have[i] && g_pawn[i] == pawn && hp < g_hp[i] && g_hp[i] > 0) {
                const int team = mem::read<uint8_t>(pawn + schema::C_BaseEntity::m_iTeamNum);
                const bool mate = cfg::combat::team_check && local_team >= 2 && team >= 2 && team == local_team;
                if (!mate && fired) {
                    // Sur un kill, le squelette de la victime peut etre libere pendant
                    // qu'on le lit: on ne demande les bones que si elle est encore vivante.
                    Vec3 pos{};
                    if (life == 0 && hp > 0) {
                        __try {
                            pos = game::aim_pos(pawn, true);
                        } __except (EXCEPTION_EXECUTE_HANDLER) {
                            pos = Vec3{};
                        }
                    }
                    if (pos.length() < 1.f) {
                        __try {
                            pos = game::abs_origin(pawn) + Vec3{ 0.f, 0.f, 40.f };
                        } __except (EXCEPTION_EXECUTE_HANDLER) {
                            pos = Vec3{};
                        }
                    }
                    trigger(hp <= 0, now, pos, g_hp[i] - hp);
                }
            }

            g_pawn[i] = pawn;
            g_hp[i] = hp;
            g_have[i] = true;
        }
    }

    static void draw_cross(ImDrawList* dl, float cx, float cy, ImU32 col, ImU32 shd, float scale) {
        const float gap = 4.f * scale;
        const float len = 9.f * scale;
        const float th = 1.0f;
        auto arm = [&](float x0, float y0, float x1, float y1) {
            dl->AddLine(ImVec2(x0 + 0.5f, y0 + 0.5f), ImVec2(x1 + 0.5f, y1 + 0.5f), shd, th);
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col, th);
        };
        arm(cx - gap - len, cy - gap - len, cx - gap, cy - gap);
        arm(cx + gap, cy - gap, cx + gap + len, cy - gap - len);
        arm(cx - gap - len, cy + gap + len, cx - gap, cy + gap);
        arm(cx + gap, cy + gap, cx + gap + len, cy + gap + len);
    }

    static void draw(int sw, int sh) {
        const DWORD now = GetTickCount();
        if (!g_until || now >= g_until)
            return;
        auto* dl = ImGui::GetForegroundDrawList();
        if (!dl) return;

        const float t = static_cast<float>(g_until - now) / static_cast<float>(g_kill ? k_kill_ms : k_hit_ms);
        int a = static_cast<int>(255.f * t);
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        const ImU32 col = g_kill ? IM_COL32(255, 48, 48, a) : IM_COL32(255, 255, 255, a);
        const ImU32 shd = IM_COL32(0, 0, 0, a / 2);

        draw_cross(dl, sw * 0.5f, sh * 0.5f, col, shd, 1.f);

        if (g_hit_world) {
            const auto vm = game::view_matrix();
            Vec2 s{};
            if (vm.world_to_screen(g_hit_pos, s, sw, sh)) {
                draw_cross(dl, s.x, s.y, col, shd, 0.85f);
                if (g_dmg > 0) {
                    char dmg[16]{};
                    std::snprintf(dmg, sizeof(dmg), "-%d", g_dmg);
                    dl->AddText(ImVec2(s.x + 10.f, s.y - 8.f), col, dmg);
                }
            }
        }
    }

    void reset_hitmarker() {
        g_until = 0;
        g_kill = false;
        g_last_fire = 0;
        g_last_shots = -1;
        g_hit_world = false;
        g_dmg = 0;
        for (int i = 0; i < 65; ++i) {
            g_hp[i] = 0;
            g_pawn[i] = 0;
            g_have[i] = false;
        }
    }

    void run_hitmarker(int screen_w, int screen_h) {
        __try {
            poll();
            if (cfg::visuals::hitmarker)
                draw(screen_w, screen_h);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}
