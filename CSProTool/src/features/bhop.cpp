#include "features/bhop.hpp"
#include "features/ssg.hpp"
#include "features/ragebot.hpp"
#include "sdk/entity.hpp"
#include "hooks/createmove.hpp"
#include <cmath>

namespace features {
    static constexpr int k_plus = 65537;
    static constexpr int k_minus = 256;

    static float g_cmd_fwd = 0.f;
    static float g_cmd_left = 0.f;
    static bool g_cmd_wish = false;

    static bool grounded(uintptr_t local) {
        if (!mem::valid(local)) return false;
        return (mem::read<int>(local + schema::C_BaseEntity::m_fFlags) & 1) != 0;
    }

    static bool in_water(uintptr_t local) {
        const float wl = mem::read<float>(local + schema::C_BaseEntity::m_flWaterLevel);
        return std::isfinite(wl) && wl > 1.f;
    }

    static bool blocked_move_type(uintptr_t local) {
        const uint8_t mt = mem::read<uint8_t>(local + schema::C_BaseEntity::m_MoveType);
        return mt == 7 || mt == 8 || mt == 9; // noclip / observer / ladder
    }

    // CS2 air wishspeed cap (~30). atan(cap/speed) = angle qui accelere sans casser la velo.
    static float ideal_strafe_rad(float speed) {
        constexpr float k_air_wish = 30.f;
        const float s = (speed > 1.f) ? speed : 1.f;
        float ang = std::atan2(k_air_wish, s);
        if (ang < 0.02f) ang = 0.02f;
        if (ang > 1.5708f) ang = 1.5708f;
        return ang;
    }

    static void wish_from_velocity(const Vec3& vel, const Vec3& view, int turn, float& fwd, float& left) {
        constexpr float k_pi = 3.14159265f;
        const float speed = vel.length2d();
        fwd = 0.f;
        left = 0.f;
        if (speed <= 20.f) {
            left = (turn > 0) ? 1.f : -1.f;
            return;
        }
        const float vel_yaw = std::atan2(vel.y, vel.x);
        const float wish_yaw = vel_yaw + ((turn > 0) ? -ideal_strafe_rad(speed) : ideal_strafe_rad(speed));
        const float wx = std::cos(wish_yaw);
        const float wy = std::sin(wish_yaw);
        const float cy = std::cos(view.y * (k_pi / 180.f));
        const float sy = std::sin(view.y * (k_pi / 180.f));
        fwd = wx * cy + wy * sy;
        left = wx * sy - wy * cy;
        const float len = std::sqrt(fwd * fwd + left * left);
        if (len > 0.01f) {
            fwd /= len;
            left /= len;
        }
    }

    static bool key_down(int vk) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    static bool user_holds_forward() {
        return key_down('W') || key_down('Z');
    }

    static bool user_holds_back() {
        return key_down('S');
    }

    static bool user_holds_left() {
        return key_down('A') || key_down('Q');
    }

    static bool user_holds_right() {
        return key_down('D');
    }

    static bool user_holds_move() {
        return user_holds_forward() || user_holds_back()
            || user_holds_left() || user_holds_right();
    }

    static float wrap_yaw(float d) {
        while (d > 180.f) d -= 360.f;
        while (d < -180.f) d += 360.f;
        return d;
    }

    static void write_btn(uintptr_t addr, bool down) {
        mem::write<int>(addr, down ? k_plus : k_minus);
    }

    // Analog CS2 : forward +1/-1, left +1 = gauche, -1 = droite.
    static void wish_from_keys(float& fwd, float& left) {
        fwd = 0.f;
        left = 0.f;
        if (user_holds_forward()) fwd += 1.f;
        if (user_holds_back()) fwd -= 1.f;
        if (user_holds_left()) left += 1.f;
        if (user_holds_right()) left -= 1.f;
        const float len = std::sqrt(fwd * fwd + left * left);
        if (len > 0.01f) {
            fwd /= len;
            left /= len;
        }
    }

    static void apply_bhop() {
        if (!cfg::combat::bhop) return;
        const auto client = game::client_base();
        const auto local = game::local_pawn();
        if (!client || !mem::valid(local)) return;

        const auto jump = client + schema::btn::jump;
        if (!key_down(VK_SPACE) || features::ssg_block_jump()
            || in_water(local) || blocked_move_type(local)) {
            write_btn(jump, false);
            return;
        }
        write_btn(jump, grounded(local));
    }

    // Strafe auto souris : analog a l'angle ideal atan(30/speed), pas 90 deg.
    static void apply_mouse_strafe() {
        if (features::ragebot_holding_stop())
            return;
        const auto client = game::client_base();
        const auto local = game::local_pawn();
        if (!client || !mem::valid(local))
            return;

        const auto left_addr = client + schema::btn::left;
        const auto right_addr = client + schema::btn::right;

        static bool forced_left = false;
        static bool forced_right = false;
        static float last_yaw = 0.f;
        static bool have_yaw = false;
        static int last_turn = 0;

        auto release_forced = [&]() {
            if (forced_left && !user_holds_left())
                write_btn(left_addr, false);
            if (forced_right && !user_holds_right())
                write_btn(right_addr, false);
            forced_left = false;
            forced_right = false;
        };

        const Vec3 view = game::view_angles();
        if (!have_yaw) {
            last_yaw = view.y;
            have_yaw = true;
        }
        const float yaw_delta = wrap_yaw(view.y - last_yaw);
        last_yaw = view.y;

        if (!cfg::combat::airstrafe || cfg::menu_open || grounded(local)
            || user_holds_left() || user_holds_right() || user_holds_back()
            || features::ssg_block_jump() || features::ssg_want_spread()
            || in_water(local) || blocked_move_type(local)) {
            release_forced();
            last_turn = 0;
            return;
        }

        release_forced();

        const Vec3 vel = mem::read<Vec3>(local + schema::C_BaseEntity::m_vecAbsVelocity);
        const float speed = vel.length2d();

        if (std::fabs(yaw_delta) > 0.12f)
            last_turn = (yaw_delta > 0.f) ? 1 : -1;
        else if (speed < 18.f)
            last_turn = 0;

        int turn = last_turn;
        if (turn == 0 && speed > 18.f) {
            const float vel_yaw_deg = std::atan2(vel.y, vel.x) * (180.f / 3.14159265f);
            const float off = wrap_yaw(view.y - vel_yaw_deg);
            if (std::fabs(off) > 0.8f)
                turn = (off > 0.f) ? 1 : -1;
            else
                turn = last_turn ? last_turn : 1;
            last_turn = turn;
        }
        if (turn == 0) {
            game::set_wish_move(local, 0.f, 0.f);
            g_cmd_fwd = 0.f;
            g_cmd_left = 0.f;
            g_cmd_wish = true;
            return;
        }

        float fwd = 0.f;
        float left = 0.f;
        wish_from_velocity(vel, view, turn, fwd, left);
        if (std::fabs(fwd) < 0.01f && std::fabs(left) < 0.01f)
            return;

        game::set_wish_move(local, fwd, left);
        g_cmd_fwd = fwd;
        g_cmd_left = left;
        g_cmd_wish = true;
    }

    // D / S / A en l'air : analog seulement. Jamais la camera.
    static void apply_directional_strafe() {
        g_cmd_wish = false;
        if (features::ragebot_holding_stop())
            return;
        if (!cfg::combat::airstrafe || cfg::menu_open)
            return;

        const auto local = game::local_pawn();
        if (!mem::valid(local) || grounded(local)
            || features::ssg_block_jump() || features::ssg_want_spread()
            || in_water(local) || blocked_move_type(local))
            return;

        const bool hold_l = user_holds_left();
        const bool hold_r = user_holds_right();
        const bool hold_b = user_holds_back();
        if (!hold_b && !hold_l && !hold_r)
            return;

        float wf = 0.f, wl = 0.f;
        const Vec3 vel = mem::read<Vec3>(local + schema::C_BaseEntity::m_vecAbsVelocity);
        if ((hold_l != hold_r) && !hold_b && vel.length2d() > 20.f) {
            wish_from_velocity(vel, game::view_angles(), hold_l ? 1 : -1, wf, wl);
        } else {
            wish_from_keys(wf, wl);
        }
        if (std::fabs(wf) < 0.01f && std::fabs(wl) < 0.01f)
            return;

        game::set_wish_move(local, wf, wl);
        g_cmd_fwd = wf;
        g_cmd_left = wl;
        g_cmd_wish = true;
    }

    static void apply_fast_stop() {
        const auto client = game::client_base();
        const auto local = game::local_pawn();
        if (!client || !mem::valid(local))
            return;

        const auto fwd_addr = client + schema::btn::forward;
        const auto back_addr = client + schema::btn::back;
        const auto left_addr = client + schema::btn::left;
        const auto right_addr = client + schema::btn::right;

        static bool forced_fwd = false;
        static bool forced_back = false;
        static bool forced_left = false;
        static bool forced_right = false;

        auto release_forced = [&]() {
            if (forced_fwd && !user_holds_forward())
                write_btn(fwd_addr, false);
            if (forced_back && !user_holds_back())
                write_btn(back_addr, false);
            if (forced_left && !user_holds_left())
                write_btn(left_addr, false);
            if (forced_right && !user_holds_right())
                write_btn(right_addr, false);
            forced_fwd = forced_back = forced_left = forced_right = false;
        };

        if (!cfg::combat::fast_stop || cfg::menu_open || !grounded(local)
            || user_holds_move() || key_down(VK_SPACE)
            || features::ssg_block_jump()) {
            release_forced();
            return;
        }

        const Vec3 vel = mem::read<Vec3>(local + schema::C_BaseEntity::m_vecAbsVelocity);
        if (vel.length2d() < 30.f) {
            release_forced();
            return;
        }

        const Vec3 view = game::view_angles();
        const float yaw = view.y * (3.14159265f / 180.f);
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        const float fdot = vel.x * cy + vel.y * sy;
        const float sdot = vel.x * sy - vel.y * cy;

        write_btn(fwd_addr, fdot < -12.f);
        write_btn(back_addr, fdot > 12.f);
        write_btn(left_addr, sdot > 12.f);
        write_btn(right_addr, sdot < -12.f);
    }

    static void apply_ssg_ground_stop() {
        if (!features::ssg_block_jump())
            return;

        const auto client = game::client_base();
        const auto local = game::local_pawn();
        if (!client || !mem::valid(local))
            return;

        const auto fwd_addr = client + schema::btn::forward;
        const auto back_addr = client + schema::btn::back;
        const auto left_addr = client + schema::btn::left;
        const auto right_addr = client + schema::btn::right;

        float wish_fwd = 0.f, wish_left = 0.f;
        game::wish_from_velocity(local, wish_fwd, wish_left);

        if (mem::read<Vec3>(local + schema::C_BaseEntity::m_vecAbsVelocity).length2d() < 15.f) {
            write_btn(fwd_addr, false);
            write_btn(back_addr, false);
            write_btn(left_addr, false);
            write_btn(right_addr, false);
            return;
        }

        write_btn(fwd_addr, wish_fwd > 0.5f);
        write_btn(back_addr, wish_fwd < -0.5f);
        write_btn(left_addr, wish_left > 0.5f);
        write_btn(right_addr, wish_left < -0.5f);
    }

    void bhop_sync_cmd(void*, std::int64_t) {
        if (features::ragebot_holding_stop())
            return;
        if (!g_cmd_wish)
            return;
        game::set_wish_move(game::local_pawn(), g_cmd_fwd, g_cmd_left);
    }

    void shutdown_no_ally_clip() {}
    void clear_ally_clip_cache() {}

    void run_bhop_tick() {
        if (!game::local_alive()) {
            g_cmd_wish = false;
            const auto client = game::client_base();
            if (client)
                write_btn(client + schema::btn::jump, false);
            return;
        }
        apply_bhop();
        apply_directional_strafe();
        apply_mouse_strafe();
        apply_fast_stop();
        apply_ssg_ground_stop();
    }

    void run_bhop() {
        if (!game::local_alive())
            return;
        if (hooks::createmove_active())
            return;
        apply_bhop();
        apply_directional_strafe();
        apply_mouse_strafe();
        apply_fast_stop();
        apply_ssg_ground_stop();
    }
}
