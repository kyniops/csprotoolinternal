#include "features/tracers.hpp"
#include "features/config.hpp"
#include "sdk/entity.hpp"
#include "sdk/spread.hpp"
#include "sdk/schema.hpp"
#include "imgui.h"
#include <cmath>
#include <cstring>

namespace features {
    static constexpr int k_max_tr = 96;
    static constexpr float k_len = 3200.f;
    static constexpr int k_proj_steps = 28;

    enum TrKind : int { TrMine = 0, TrEnemy = 1, TrAlly = 2 };

    struct Tracer {
        Vec3 a{};
        Vec3 b{};
        DWORD until = 0;
        DWORD born = 0;
        int kind = TrEnemy;
        bool used = false;
    };

    static Tracer g_tr[k_max_tr]{};
    static int g_last_shots[65]{};
    static uintptr_t g_last_pawn[65]{};

    void reset_tracers();

    static Vec3 ang_fwd(const Vec3& ang) {
        const float p = ang.x * (3.14159265f / 180.f);
        const float y = ang.y * (3.14159265f / 180.f);
        const float cp = std::cos(p);
        return { cp * std::cos(y), cp * std::sin(y), -std::sin(p) };
    }

    static float ray_dist(const Vec3& a, const Vec3& dir, const Vec3& p, float* t_out) {
        const Vec3 d = p - a;
        const float t = d.x * dir.x + d.y * dir.y + d.z * dir.z;
        if (t_out) *t_out = t;
        if (t < 0.f) return 1e9f;
        const Vec3 closest = a + dir * t;
        return (p - closest).length();
    }

    static Vec3 clip_end(const Vec3& start, const Vec3& dir, uintptr_t shooter) {
        Vec3 end = start + dir * k_len;
        float best_t = k_len;
        for (int i = 1; i <= 64; ++i) {
            const auto ctrl = game::get_entity(static_cast<uint32_t>(i));
            if (!mem::valid(ctrl)) continue;
            const auto h = mem::read<uint32_t>(ctrl + schema::CCSPlayerController::m_hPlayerPawn);
            if (!h || h == 0xFFFFFFFFu) continue;
            const auto pawn = game::get_entity(h);
            if (!mem::valid(pawn) || pawn == shooter) continue;
            if (mem::read<int>(pawn + schema::C_BaseEntity::m_iHealth) <= 0) continue;
            if (mem::read<uint8_t>(pawn + schema::C_BaseEntity::m_lifeState) != 0) continue;
            const Vec3 origin = game::abs_origin(pawn);
            const Vec3 chest = origin + Vec3{ 0.f, 0.f, 40.f };
            float t = 0.f;
            if (ray_dist(start, dir, chest, &t) > 22.f) continue;
            if (t > 12.f && t < best_t) {
                best_t = t;
                end = start + dir * t;
            }
        }
        return end;
    }

    static Vec3 add_punch(uintptr_t pawn, Vec3 ang) {
        const auto svc = mem::read<uintptr_t>(pawn + schema::C_CSPlayerPawn::m_pAimPunchServices);
        if (!mem::valid(svc)) return ang;
        const Vec3 punch = mem::read<Vec3>(
            svc + schema::CCSPlayer_AimPunchServices::m_predictableBaseAngle);
        if (punch.length() < 0.001f || punch.length() > 40.f) return ang;
        ang.x += punch.x * 2.f;
        ang.y += punch.y * 2.f;
        if (ang.x > 89.f) ang.x = 89.f;
        if (ang.x < -89.f) ang.x = -89.f;
        return ang;
    }

    static Vec3 eye_angles(uintptr_t pawn) {
        return mem::read<Vec3>(pawn + schema::C_CSPlayerPawn::m_angEyeAngles);
    }

    static Vec3 bullet_angles(uintptr_t pawn, uintptr_t wpn, const Vec3& base, float recoil_idx) {
        if (!mem::valid(wpn) || !std::isfinite(base.x) || !std::isfinite(base.y))
            return base;

        float spr = spread::live_spread(wpn);
        const float inacc = spread::fire_inaccuracy(pawn, wpn, &spr);
        if (inacc + spr < 0.00001f)
            return base;

        if (recoil_idx < 0.f) recoil_idx = 0.f;
        const uint16_t item = spread::item_index(wpn);
        const int tb = spread::controller_tick();
        const int t = tb > 0 ? tb : spread::client_tick();

        uint32_t seed = spread::engine_seed(pawn, base, static_cast<uint32_t>(t > 0 ? t : 1));
        if (!seed)
            seed = spread::compute_seed(base, t > 0 ? t : 1);

        float sx = 0.f, sy = 0.f;
        spread::engine_spread_xy(seed, item, inacc, spr, recoil_idx, sx, sy);

        Vec3 out = spread::apply_spread_ang(base, sx, sy);
        if (out.x > 89.f) out.x = 89.f;
        if (out.x < -89.f) out.x = -89.f;
        out.y = spread::ang_norm(out.y);
        return out;
    }

    static bool is_gun(uint16_t def) {
        if (def == 0) return false;
        if (def == 42 || def == 59 || def == 80) return false;
        if (def >= 500 && def < 600) return false;
        if (def >= 43 && def <= 49) return false;
        return true;
    }

    static void push_tr(const Vec3& a, const Vec3& b, int kind, DWORD now, float life) {
        // Un NaN stocke ici ressort a l'ecran des frames plus tard, dans ImGui.
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z)
            || !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z))
            return;
        int slot = -1;
        DWORD oldest = 0xFFFFFFFFu;
        for (int i = 0; i < k_max_tr; ++i) {
            if (!g_tr[i].used) { slot = i; break; }
            if (g_tr[i].until < oldest) {
                oldest = g_tr[i].until;
                slot = i;
            }
        }
        if (slot < 0) return;
        g_tr[slot].a = a;
        g_tr[slot].b = b;
        g_tr[slot].kind = kind;
        g_tr[slot].born = now;
        g_tr[slot].until = now + static_cast<DWORD>(life * 1000.f);
        g_tr[slot].used = true;
    }

    static void spawn_tracer(uintptr_t pawn, uintptr_t local, int local_team, DWORD now, float recoil_idx) {
        if (!cfg::visuals::tracers) return;
        if (!mem::valid(pawn)) return;
        if (mem::read<int>(pawn + schema::C_BaseEntity::m_iHealth) <= 0) return;
        if (mem::read<uint8_t>(pawn + schema::C_BaseEntity::m_lifeState) != 0) return;

        const auto wpn = game::active_weapon(pawn);
        if (mem::valid(wpn) && !is_gun(spread::item_index(wpn)))
            return;

        const bool mine = (pawn == local);
        const int team = mem::read<uint8_t>(pawn + schema::C_BaseEntity::m_iTeamNum);
        const bool ally = !mine && local_team >= 2 && team >= 2 && team == local_team;
        const int kind = mine ? TrMine : (ally ? TrAlly : TrEnemy);

        if (kind == TrMine && !cfg::visuals::tracers_local) return;
        if (kind == TrEnemy && !cfg::visuals::tracers_enemy) return;
        if (kind == TrAlly && !cfg::visuals::tracers_ally) return;

        Vec3 ang = mine ? add_punch(pawn, game::view_angles()) : eye_angles(pawn);
        if (!std::isfinite(ang.x) || !std::isfinite(ang.y)) return;
        if (mem::valid(wpn))
            ang = bullet_angles(pawn, wpn, ang, recoil_idx);

        const Vec3 dir = ang_fwd(ang).normalized();
        if (dir.length() < 0.1f) return;

        const Vec3 eye = game::eye_pos(pawn);
        const float start_off = mine ? 24.f : 14.f;
        const Vec3 start = eye + dir * start_off;
        const Vec3 end = clip_end(start, dir, pawn);

        const float life = cfg::visuals::tracers_time;
        if (life < 0.2f) return;
        push_tr(start, end, kind, now, life);
    }

    static void track_shots(int idx, uintptr_t pawn, uintptr_t local, int local_team, DWORD now) {
        const int shots = mem::read<int>(pawn + schema::C_CSPlayerPawn::m_iShotsFired);
        int& last = g_last_shots[idx];
        if (g_last_pawn[idx] != pawn) {
            last = shots;
            g_last_pawn[idx] = pawn;
            return;
        }
        if (shots < 0 || shots > 80) {
            last = shots;
            return;
        }
        const int delta = shots - last;
        last = shots;
        if (delta <= 0 || delta > 8) return;

        const auto wpn = game::active_weapon(pawn);
        float recoil = 0.f;
        if (mem::valid(wpn))
            recoil = mem::read<float>(wpn + schema::C_CSWeaponBase::m_flRecoilIndex);

        for (int n = 0; n < delta; ++n) {
            const float ri = recoil - static_cast<float>(delta - 1 - n);
            spawn_tracer(pawn, local, local_team, now, ri < 0.f ? 0.f : ri);
        }
    }

    static void poll() {
        const DWORD now = GetTickCount();
        if (!game::world_ready() || !game::local_alive()) {
            reset_tracers();
            return;
        }
        const auto local = game::local_pawn();
        if (!mem::valid(local)) {
            reset_tracers();
            return;
        }
        const int local_team = mem::read<uint8_t>(local + schema::C_BaseEntity::m_iTeamNum);

        track_shots(0, local, local, local_team, now);

        for (int i = 1; i <= 64; ++i) {
            const auto ctrl = game::get_entity(static_cast<uint32_t>(i));
            if (!mem::valid(ctrl)) {
                g_last_pawn[i] = 0;
                continue;
            }
            const auto h = mem::read<uint32_t>(ctrl + schema::CCSPlayerController::m_hPlayerPawn);
            if (!h || h == 0xFFFFFFFFu) {
                g_last_pawn[i] = 0;
                continue;
            }
            const auto pawn = game::get_entity(h);
            if (!mem::valid(pawn) || pawn == local) {
                g_last_pawn[i] = 0;
                continue;
            }
            if (mem::read<int>(pawn + schema::C_BaseEntity::m_iHealth) <= 0) {
                g_last_pawn[i] = 0;
                continue;
            }
            track_shots(i, pawn, local, local_team, now);
        }
    }

    static ImU32 kind_col(int kind, int a) {
        const float* c = cfg::visuals::tracers_color_enemy;
        if (kind == TrMine) c = cfg::visuals::tracers_color_local;
        else if (kind == TrAlly) c = cfg::visuals::tracers_color_ally;
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        return IM_COL32(
            (int)(c[0] * 255.f),
            (int)(c[1] * 255.f),
            (int)(c[2] * 255.f),
            a);
    }

    static bool project_segment(const ViewMatrix& vm, const Vec3& a, const Vec3& b,
        int sw, int sh, Vec2& out_a, Vec2& out_b) {
        Vec2 pts[k_proj_steps + 1]{};
        bool vis[k_proj_steps + 1]{};
        for (int i = 0; i <= k_proj_steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(k_proj_steps);
            const Vec3 p = a + (b - a) * t;
            vis[i] = vm.world_to_screen(p, pts[i], sw, sh);
        }

        int best_start = -1;
        int best_len = 0;
        int cur_start = -1;
        int cur_len = 0;
        for (int i = 0; i <= k_proj_steps; ++i) {
            if (vis[i]) {
                if (cur_start < 0) cur_start = i;
                ++cur_len;
            } else {
                if (cur_len > best_len) {
                    best_len = cur_len;
                    best_start = cur_start;
                }
                cur_start = -1;
                cur_len = 0;
            }
        }
        if (cur_len > best_len) {
            best_len = cur_len;
            best_start = cur_start;
        }
        if (best_len < 2 || best_start < 0)
            return false;

        out_a = pts[best_start];
        out_b = pts[best_start + best_len - 1];
        return true;
    }

    static void draw(int sw, int sh) {
        const DWORD now = GetTickCount();
        auto* dl = ImGui::GetForegroundDrawList();
        if (!dl) return;
        const auto vm = game::view_matrix();

        for (int i = 0; i < k_max_tr; ++i) {
            auto& t = g_tr[i];
            if (!t.used) continue;
            if (now >= t.until) {
                t.used = false;
                continue;
            }
            float span = static_cast<float>(t.until - t.born);
            if (span < 1.f) span = 1.f;
            float k = static_cast<float>(t.until - now) / span;
            if (k < 0.f) k = 0.f;
            if (k > 1.f) k = 1.f;
            const int a = static_cast<int>(255.f * k);
            if (a < 8) continue;

            Vec2 sa{}, sb{};
            if (!project_segment(vm, t.a, t.b, sw, sh, sa, sb))
                continue;
            if (!std::isfinite(sa.x) || !std::isfinite(sa.y)
                || !std::isfinite(sb.x) || !std::isfinite(sb.y))
                continue;

            const ImU32 col = kind_col(t.kind, a);
            const ImU32 glow = kind_col(t.kind, a / 3);
            dl->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), glow, 3.f);
            dl->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), col, 1.25f);
        }
    }

    void reset_tracers() {
        for (int i = 0; i < k_max_tr; ++i)
            g_tr[i] = {};
        for (int i = 0; i < 65; ++i) {
            g_last_shots[i] = 0;
            g_last_pawn[i] = 0;
        }
    }

    void run_tracers(int screen_w, int screen_h) {
        __try {
            poll();
            if (cfg::visuals::tracers)
                draw(screen_w, screen_h);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}
