#pragma once
#include <Windows.h>
#include "sdk/entity.hpp"
#include "features/silent.hpp"
#include "core/status.hpp"
#include <cmath>

namespace target {
    inline uintptr_t g_skip_pawn = 0;
    inline DWORD g_skip_until = 0;
    inline void skip_pawn_until(uintptr_t pawn, DWORD until_ms) {
        g_skip_pawn = pawn;
        g_skip_until = until_ms;
    }
    inline bool is_skipped(uintptr_t /*pawn*/) {
        return false; // desactive : bloquait les tirs multi-cibles
    }

    inline Vec3 apply_rcs(Vec3 aim, uintptr_t local) {
        if (!mem::valid(local)) return aim;
        const int shots = mem::read<int>(local + schema::C_CSPlayerPawn::m_iShotsFired);
        if (shots < 1 || shots > 40) return aim;

        const auto svc = mem::read<uintptr_t>(local + schema::C_CSPlayerPawn::m_pAimPunchServices);
        if (!mem::valid(svc)) return aim;

        const Vec3 punch = mem::read<Vec3>(
            svc + schema::CCSPlayer_AimPunchServices::m_predictableBaseAngle);
        if (punch.length() < 0.001f || punch.length() > 40.f) return aim;

        aim.x -= punch.x * 2.f;
        aim.y -= punch.y * 2.f;
        normalize_angles(aim);
        return aim;
    }

    // Os extra pour le peek (bras / buste / tete) en plus du squelette ESP.
    inline constexpr int k_fov_bones[] = {
        1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23
    };

    inline bool point_in_fov(const Vec2& scr, const Vec2& center, float fov_px, float& dist) {
        dist = Vec2{ scr.x - center.x, scr.y - center.y }.length();
        return dist < fov_px;
    }

    // Plus petite distance ecran d'une partie du corps au centre. -1 si rien a l'ecran.
    inline float closest_body_in_fov(const game::Player& p, const ViewMatrix& vm,
                                     int sw, int sh, const Vec2& center, float fov_px) {
        float best = 1.0e9f;
        bool any = false;

        const Vec3 pts[] = { p.head, p.origin, p.feet };
        for (const auto& w : pts) {
            if (w.length() < 1.f) continue;
            Vec2 s{};
            if (!vm.world_to_screen(w, s, sw, sh)) continue;
            float d = 0.f;
            if (!point_in_fov(s, center, fov_px, d)) continue;
            if (d < best) best = d;
            any = true;
        }

        for (int id : k_fov_bones) {
            const Vec3 w = game::bone_pos(p.pawn, id, p.bone_lift);
            if (w.length() < 1.f) continue;
            Vec2 s{};
            if (!vm.world_to_screen(w, s, sw, sh)) continue;
            float d = 0.f;
            if (!point_in_fov(s, center, fov_px, d)) continue;
            if (d < best) best = d;
            any = true;
        }
        return any ? best : -1.f;
    }

    inline float angle_fov(const Vec3& view, const Vec3& aim) {
        const float dp = aim.x - view.x;
        float dy = aim.y - view.y;
        while (dy > 180.f) dy -= 360.f;
        while (dy < -180.f) dy += 360.f;
        return std::sqrt(dp * dp + dy * dy);
    }

    inline bool is_focus(const game::Player& p) {
        return cfg::combat::focus_id != 0 && game::player_id(p) == cfg::combat::focus_id;
    }

    // full_screen = 360 (hors ecran). whole_screen = n'importe qui visible a l'ecran.
    inline bool find_head(Vec3& out_angles, uintptr_t* out_pawn, bool full_screen,
                          bool ignore_walls = false, bool whole_screen = false) {
        if (!game::world_ready() || !game::local_alive()) return false;
        const auto local = game::local_pawn();
        if (!mem::valid(local)) return false;
        if (mem::read<int>(local + schema::C_BaseEntity::m_iHealth) <= 0) return false;

        int sw = silent::screen_w.load(std::memory_order_relaxed);
        int sh = silent::screen_h.load(std::memory_order_relaxed);
        if (sw < 16) sw = 1920;
        if (sh < 16) sh = 1080;

        const Vec3 eye = game::eye_pos(local);
        const Vec3 view = game::view_angles();
        const auto vm = game::view_matrix();
        const Vec2 center{ sw * 0.5f, sh * 0.5f };
        const float fov_px = whole_screen
            ? (std::sqrt(static_cast<float>(sw * sw + sh * sh)) * 0.5f + 16.f)
            : (std::max)(8.f, cfg::combat::fov);
        const bool locked = cfg::combat::focus_id != 0;

        float best = 1.0e9f;
        Vec3 best_head{};
        uintptr_t best_pawn = 0;
        bool found = false;
        int considered = 0;

        for (const auto& p : game::collect_players(!cfg::combat::team_check)) {
            if (cfg::combat::team_check && p.teammate && !is_focus(p)) continue;
            if (locked && !is_focus(p)) continue;
            if (is_skipped(p.pawn)) continue;

            if (!game::wall_ok(p.pawn, ignore_walls))
                continue;

            Vec3 head = game::aim_pos(p.pawn, cfg::combat::aim_body);
            if (head.length() < 1.f)
                head = p.head;
            if (head.length() < 1.f)
                continue;

            float fov_score = 0.f;
            if (full_screen || locked) {
                const Vec3 ang = calc_angle(eye, head);
                fov_score = angle_fov(view, ang);
            } else {
                const float body_d = closest_body_in_fov(p, vm, sw, sh, center, fov_px);
                if (body_d < 0.f || body_d >= fov_px)
                    continue;
                fov_score = body_d;
            }

            float score = fov_score;
            if (!locked) {
                if (cfg::combat::aim_priority == 1)
                    score = static_cast<float>(p.health) + fov_score * 0.001f;
                else if (cfg::combat::aim_priority == 2)
                    score = (head - eye).length() + fov_score * 0.01f;
            }
            if (cfg::combat::autowall && cfg::combat::wall_check && !game::is_visible(p.pawn))
                score += 80.f;

            ++considered;
            if (score >= best)
                continue;

            best = score;
            best_head = head;
            best_pawn = p.pawn;
            found = true;
        }

        tools::last_targets.store(considered, std::memory_order_relaxed);
        if (!found) return false;

        Vec3 ang = calc_angle(eye, best_head);
        normalize_angles(ang);
        out_angles = apply_rcs(ang, local);
        if (out_pawn) *out_pawn = best_pawn;
        return true;
    }

    // SSG jump : FOV souple + retourne le pawn (requis pour autoshoot ragebot).
    inline bool find_ssg_head(Vec3& out_angles, uintptr_t* out_pawn = nullptr) {
        if (out_pawn) *out_pawn = 0;
        if (cfg::combat::silent_360)
            return find_head(out_angles, out_pawn, true, false, false);
        if (cfg::combat::silent)
            return find_head(out_angles, out_pawn, false, false, true);

        if (!game::world_ready() || !game::local_alive()) return false;
        const auto local = game::local_pawn();
        if (!mem::valid(local)) return false;
        if (mem::read<int>(local + schema::C_BaseEntity::m_iHealth) <= 0) return false;

        int sw = silent::screen_w.load(std::memory_order_relaxed);
        int sh = silent::screen_h.load(std::memory_order_relaxed);
        if (sw < 16) sw = 1920;
        if (sh < 16) sh = 1080;

        const Vec3 eye = game::eye_pos(local);
        const Vec3 view = game::view_angles();
        const auto vm = game::view_matrix();
        const Vec2 center{ sw * 0.5f, sh * 0.5f };
        const float fov_px = (std::max)(cfg::combat::fov * 1.35f, 160.f);
        constexpr float k_max_ang = 28.f;

        float best = 1.0e9f;
        Vec3 best_head{};
        uintptr_t best_pawn = 0;
        bool found = false;

        for (const auto& p : game::collect_players(!cfg::combat::team_check)) {
            if (cfg::combat::team_check && p.teammate && !is_focus(p))
                continue;
            if (is_skipped(p.pawn)) continue;
            if (cfg::combat::focus_id && !is_focus(p))
                continue;

            Vec3 head = game::aim_pos(p.pawn, cfg::combat::aim_body);
            if (head.length() < 1.f)
                head = p.head;
            if (head.length() < 1.f)
                continue;

            const auto vel = mem::read<Vec3>(p.pawn + schema::C_BaseEntity::m_vecAbsVelocity);
            if (std::isfinite(vel.x) && vel.length() < 350.f)
                head = head + vel * (0.5f / 64.f);

            if (cfg::combat::focus_id && is_focus(p)) {
                best_head = head;
                best_pawn = p.pawn;
                found = true;
                break;
            }

            Vec2 hs{};
            if (!vm.world_to_screen(head, hs, sw, sh))
                continue;
            float head_d = 0.f;
            if (!point_in_fov(hs, center, fov_px, head_d))
                continue;

            Vec3 ang = calc_angle(eye, head);
            normalize_angles(ang);
            if (angle_fov(view, ang) > k_max_ang)
                continue;

            const bool vis = game::is_visible(p.pawn);
            if (!vis) {
                if (!game::wall_ok(p.pawn, false))
                    continue;
                if (!cfg::combat::autowall && head_d > fov_px * 0.65f)
                    continue;
            }

            if (head_d >= best)
                continue;
            best = head_d;
            best_head = head;
            best_pawn = p.pawn;
            found = true;
        }

        if (!found) return false;
        out_angles = calc_angle(eye, best_head);
        normalize_angles(out_angles);
        if (out_pawn) *out_pawn = best_pawn;
        return true;
    }
}
