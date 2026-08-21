#include "features/ragebot.hpp"
#include "features/config.hpp"
#include "features/target.hpp"
#include "sdk/entity.hpp"
#include "sdk/spread.hpp"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace features {
    static constexpr int k_plus = 65537;
    static constexpr int k_minus = 256;
    static constexpr uint16_t k_ssg = 40;

    static float g_last_hc = 0.f;
    static int g_multitap_left = 0;
    static int g_attack_hold = 0;
    static bool g_forced_atk = false;
    static bool g_holding_stop = false;
    static int g_stop_ticks = 0;
    static bool g_forced_fwd = false;
    static bool g_forced_back = false;
    static bool g_forced_left = false;
    static bool g_forced_right = false;
    static DWORD g_ssg_cd_until = 0; // debounce sol court seulement
    static float g_prev_vz = 0.f;
    static bool g_was_ground = true;
    static bool g_air_rose = false;
    static int g_ground_ticks = 0;
    static bool g_want_silent = false;   // silent cmd seulement sur le tick de tir
    static int g_ssg_scope_phase = 0; // 0 idle, 1 press scope, 2..N wait zoom

    static void write_btn(uintptr_t addr, bool down) {
        mem::write<int>(addr, down ? k_plus : k_minus);
    }

    static void write_attack(bool down) {
        const auto client = game::client_base();
        if (!client) return;
        mem::write<int>(client + schema::btn::attack, down ? k_plus : k_minus);
    }

    static bool key_down(int vk) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    static void release_forced_move() {
        const auto client = game::client_base();
        if (!client) {
            g_forced_fwd = g_forced_back = g_forced_left = g_forced_right = false;
            return;
        }
        if (g_forced_fwd && !key_down('W') && !key_down('Z'))
            write_btn(client + schema::btn::forward, false);
        if (g_forced_back && !key_down('S'))
            write_btn(client + schema::btn::back, false);
        if (g_forced_left && !key_down('A') && !key_down('Q'))
            write_btn(client + schema::btn::left, false);
        if (g_forced_right && !key_down('D'))
            write_btn(client + schema::btn::right, false);
        g_forced_fwd = g_forced_back = g_forced_left = g_forced_right = false;
    }

    static void release_forced_attack() {
        if (!g_forced_atk && g_attack_hold <= 0) return;
        if (!key_down(VK_LBUTTON))
            write_attack(false);
        g_forced_atk = false;
        g_attack_hold = 0;
        g_multitap_left = 0;
    }

    static void press_attack_hold(int ticks) {
        if (ticks < 2) ticks = 2;
        if (ticks > 6) ticks = 6;
        write_attack(true);
        g_forced_atk = true;
        if (g_attack_hold < ticks)
            g_attack_hold = ticks;
    }

    static bool grounded(uintptr_t local) {
        if (!mem::valid(local)) return false;
        return (mem::read<int>(local + schema::C_BaseEntity::m_fFlags) & 1) != 0;
    }

    static bool is_ssg(uintptr_t wpn) {
        if (!mem::valid(wpn)) return false;
        if (spread::item_index(wpn) == k_ssg)
            return true;
        char dn[72]{};
        return game::designer_name(wpn, dn, sizeof(dn)) && std::strstr(dn, "ssg08");
    }

    static int game_tick() {
        const auto gv = mem::read<uintptr_t>(
            game::client_base() + schema::off::client_dll::dwGlobalVars);
        if (!mem::valid(gv)) return 0;
        const int a = mem::read<int>(gv + 0x44);
        if (a > 100 && a < 200000000) return a;
        const int b = mem::read<int>(gv + 0x48);
        if (b > 100 && b < 200000000) return b;
        return 0;
    }

    static bool weapon_can_fire(uintptr_t pawn, uintptr_t wpn) {
        if (!mem::valid(pawn) || !mem::valid(wpn)) return false;
        if (mem::read<int>(wpn + schema::C_BasePlayerWeapon::m_iClip1) <= 0)
            return false;
        if (mem::read<uint8_t>(wpn + schema::C_CSWeaponBase::m_bInReload))
            return false;
        const uint16_t def = spread::item_index(wpn);
        if (def == 0) return false;
        if (def == 42 || def == 59 || def == 80) return false;
        if (def >= 500 && def < 600) return false;
        if (def >= 43 && def <= 49) return false;
        if (def == 31) return false;
        const int next = mem::read<int>(wpn + schema::C_BasePlayerWeapon::m_nNextPrimaryAttackTick);
        const int tick = game_tick();
        if (def == k_ssg)
            return true; // next-attack tick trop faux sur SSG
        if (tick > 0 && next > 0 && next > tick + 2)
            return false;
        return true;
    }

    static bool ssg_is_scoped(uintptr_t pawn, uintptr_t wpn) {
        if (mem::valid(pawn) && mem::read<uint8_t>(pawn + schema::C_CSPlayerPawn::m_bIsScoped))
            return true;
        if (mem::valid(wpn) && mem::read<int>(wpn + schema::C_CSWeaponBase::m_weaponMode) == 1)
            return true;
        return false;
    }

    static void press_scope_once() {
        const auto client = game::client_base();
        if (!client) return;
        write_btn(client + schema::btn::attack2, true);
    }

    static void release_scope_btn() {
        const auto client = game::client_base();
        if (!client) return;
        write_btn(client + schema::btn::attack2, false);
    }

    // Apex : croisement prev>0 && vz<=0. Appeler AVANT update_air_state.
    // PAS de flag "deja tire" : le debounce CD suffit (sinon 1 balle puis bloque).
    static bool near_apex_vz(float vz, float prev, bool rose) {
        if (!rose)
            return false;
        if (prev > 0.f && vz <= 0.f)
            return true;
        float step = prev - vz;
        if (step < 1.f || step > 45.f)
            step = 12.5f;
        const float vz_next = vz - step;
        if (prev > 0.f && vz >= -10.f && vz <= 30.f
            && std::fabs(vz) <= std::fabs(vz_next))
            return true;
        return false;
    }

    static void update_air_state(bool on_ground, float vz) {
        const bool real_ground = on_ground && std::fabs(vz) < 90.f;
        if (real_ground) {
            ++g_ground_ticks;
            if (g_ground_ticks >= 2) {
                g_air_rose = false;
                g_was_ground = true;
            }
        } else {
            if (vz > 50.f)
                g_air_rose = true; // chaque montee (bhop) re-arme l'apex
            g_was_ground = false;
            g_ground_ticks = 0;
        }
        g_prev_vz = vz;
    }

    static void track_vz(uintptr_t local) {
        if (!mem::valid(local)) return;
        const float vz = mem::read<Vec3>(local + schema::C_BaseEntity::m_vecAbsVelocity).z;
        update_air_state(grounded(local), vz);
    }

    // Auto-Stop : inverse le vecteur vitesse (m_vecAbsVelocity) dans l'espace vue.
    // Analog protobuf UserCmd est trop fragile (GetBitRange) -> boutons + wish pawn.
    static void apply_micro_stop(uintptr_t client, uintptr_t local) {
        if (!client || !mem::valid(local)) return;
        const Vec3 vel = mem::read<Vec3>(local + schema::C_BaseEntity::m_vecAbsVelocity);
        const float spd = vel.length2d();

        // Wish oppose a la velocite (counter-strafe sur ce tick).
        float wish_fwd = 0.f, wish_left = 0.f;
        if (spd >= 3.f)
            game::wish_from_velocity(local, wish_fwd, wish_left);
        game::set_wish_move(local, wish_fwd, wish_left);

        if (spd < 3.f) {
            release_forced_move();
            write_btn(client + schema::btn::forward, false);
            write_btn(client + schema::btn::back, false);
            write_btn(client + schema::btn::left, false);
            write_btn(client + schema::btn::right, false);
            return;
        }

        const Vec3 view = game::view_angles();
        const float yaw = view.y * (3.14159265f / 180.f);
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        // Projection vitesse -> avant/strafe (meme base que le moteur).
        const float fdot = vel.x * cy + vel.y * sy;
        const float sdot = vel.x * sy - vel.y * cy;

        // Inverse : si on avance, press Back, etc.
        const bool fwd = fdot < -2.5f;
        const bool back = fdot > 2.5f;
        const bool left = sdot > 2.5f;
        const bool right = sdot < -2.5f;

        write_btn(client + schema::btn::forward, fwd);
        write_btn(client + schema::btn::back, back);
        write_btn(client + schema::btn::left, left);
        write_btn(client + schema::btn::right, right);
        g_forced_fwd = fwd;
        g_forced_back = back;
        g_forced_left = left;
        g_forced_right = right;
    }

    static void begin_stop(int ticks) {
        if (ticks < 1) ticks = 1;
        if (ticks > 3) ticks = 3;
        g_stop_ticks = (std::max)(g_stop_ticks, ticks);
        g_holding_stop = true;
        const auto client = game::client_base();
        const auto local = game::local_pawn();
        if (client && mem::valid(local))
            apply_micro_stop(client, local);
    }

    bool ragebot_on() {
        return cfg::combat::ragebot && !cfg::menu_open;
    }

    bool ragebot_holding_stop() {
        return ragebot_on() && (g_holding_stop || g_stop_ticks > 0);
    }

    float ragebot_last_hc() {
        return g_last_hc;
    }

    float ragebot_hitchance(const Vec3& aim, uintptr_t target_pawn) {
        g_last_hc = 0.f;
        const auto pawn = game::local_pawn();
        const auto wpn = game::active_weapon(pawn);
        if (!mem::valid(pawn) || !mem::valid(wpn) || !mem::valid(target_pawn))
            return 0.f;

        float spr = 0.f;
        const float inacc = spread::fire_inaccuracy(pawn, wpn, &spr);
        const float cone = inacc + spr;
        if (cone < 0.00008f) {
            g_last_hc = 100.f;
            return 100.f;
        }

        const Vec3 eye = game::eye_pos(pawn);
        Vec3 head = game::aim_pos(target_pawn, cfg::combat::aim_body);
        if (head.length() < 1.f)
            head = game::abs_origin(target_pawn) + Vec3{ 0.f, 0.f, 64.f };
        const float dist = (head - eye).length();
        if (dist < 1.f)
            return 0.f;

        const float head_r = is_ssg(wpn) ? 8.5f : 7.0f;
        const float head_ang = std::atan2(head_r, dist) * spread::k_rad;
        const float cone_ang = std::atan(cone) * spread::k_rad;
        if (cone_ang < 0.0001f) {
            g_last_hc = 100.f;
            return 100.f;
        }

        float geo = (head_ang / cone_ang) * 100.f;
        if (geo > 100.f)
            geo = 100.f;

        // Monte-Carlo hitchance (256 trajectoires dans le cone d'inaccuracy).
        const uint16_t item = spread::item_index(wpn);
        const float recoil = mem::read<float>(wpn + schema::C_CSWeaponBase::m_flRecoilIndex);
        constexpr int k_n = 256;
        int hits = 0;
        for (int i = 0; i < k_n; ++i) {
            float sx = 0.f, sy = 0.f;
            const uint32_t seed = static_cast<uint32_t>(i * 2654435761u + 97u);
            spread::calc_spread(seed, inacc, spr, item, recoil, sx, sy);
            if (spread::ang_delta(spread::apply_spread_ang(aim, sx, sy), aim) <= head_ang)
                ++hits;
        }
        const float sample_hc = 100.f * static_cast<float>(hits) / static_cast<float>(k_n);
        // Priorite simulation ; geo = filet si seed imparfait.
        g_last_hc = 0.30f * geo + 0.70f * sample_hc;

        const Vec3 vel = mem::read<Vec3>(pawn + schema::C_BaseEntity::m_vecAbsVelocity);
        const bool on_ground = grounded(pawn);
        const float spd = vel.length2d();
        if (on_ground && spd < 15.f)
            g_last_hc = (std::max)(g_last_hc, 95.f);
        if (!on_ground && is_ssg(wpn) && std::fabs(vel.z) < 28.f && spd < 55.f)
            g_last_hc = (std::max)(g_last_hc, 93.f);
        if (head_ang >= cone_ang * 0.85f)
            g_last_hc = (std::max)(g_last_hc, 90.f);
        return g_last_hc;
    }

    void ragebot_autostop_tick() {
        if (!ragebot_on() || !cfg::combat::ragebot_autostop || g_stop_ticks <= 0) {
            g_holding_stop = false;
            release_forced_move();
            return;
        }
        g_holding_stop = true;
        const auto client = game::client_base();
        const auto local = game::local_pawn();
        if (!client || !mem::valid(local)) {
            release_forced_move();
            return;
        }
        apply_micro_stop(client, local);
        --g_stop_ticks;
        if (g_stop_ticks <= 0) {
            g_holding_stop = false;
            release_forced_move();
        }
    }

    bool ragebot_allow_shot(const Vec3& aim, uintptr_t target_pawn) {
        g_holding_stop = false;
        g_want_silent = false;
        if (!ragebot_on()) {
            g_stop_ticks = 0;
            release_forced_attack();
            release_forced_move();
            return false;
        }

        // Hold d'attaque : garder le bouton SANS silent, et SANS "fire_ok" fantome.
        // (Sinon classic/is_firing re-bloque le prochain tir.)
        if (g_attack_hold > 0) {
            if (g_stop_ticks > 0)
                ragebot_autostop_tick();
            write_attack(true);
            g_forced_atk = true;
            --g_attack_hold;
            g_want_silent = false;
            return false; // pas un nouveau tir
        }

        if (g_multitap_left > 0) {
            if (g_stop_ticks > 0)
                ragebot_autostop_tick();
            press_attack_hold(1);
            --g_multitap_left;
            g_want_silent = true;
            return true;
        }

        const auto pawn = game::local_pawn();
        const auto wpn = game::active_weapon(pawn);
        if (!weapon_can_fire(pawn, wpn) || !mem::valid(target_pawn)) {
            track_vz(pawn);
            release_forced_attack();
            if (g_stop_ticks > 0)
                ragebot_autostop_tick();
            else
                release_forced_move();
            return false;
        }

        const bool ssg = is_ssg(wpn);
        const Vec3 vel = mem::read<Vec3>(pawn + schema::C_BaseEntity::m_vecAbsVelocity);
        const float spd = vel.length2d();
        const bool on_ground = grounded(pawn);

        // Apex AVANT update_air_state (sinon prev==vz).
        const bool rose_now = g_air_rose || (!on_ground && vel.z > 50.f);
        const bool apex = on_ground
            ? true
            : near_apex_vz(vel.z, g_prev_vz, rose_now);
        const bool peak_zone = !on_ground && rose_now
            && vel.z <= 20.f && vel.z >= -40.f;
        update_air_state(on_ground, vel.z);

        // === SSG : tant qu'il y a une cible + precis -> TIRE (debounce court) ===
        if (ssg) {
            const DWORD now = GetTickCount();

            if (!cfg::combat::ragebot_autoshoot && !key_down(VK_LBUTTON))
                return false;

            // Debounce tres court (anti double-tick), pas un lock.
            if (now < g_ssg_cd_until) {
                release_forced_move();
                g_want_silent = false;
                return false;
            }

            if (!on_ground) {
                if (cfg::combat::ragebot_air_apex && !apex && !peak_zone) {
                    release_forced_move();
                    g_stop_ticks = 0;
                    return false;
                }
                if (cfg::combat::ragebot_autostop && spd > 8.f)
                    begin_stop(1);
            } else {
                if (cfg::combat::ragebot_autostop && spd > 8.f) {
                    begin_stop(2);
                    ragebot_autostop_tick();
                    if (spd > 20.f)
                        return false;
                }
            }

            g_ssg_scope_phase = 0;
            game::force_no_spread(pawn);
            write_attack(true);
            g_forced_atk = true;
            g_attack_hold = 1; // 1 tick hold max
            g_multitap_left = 0;
            g_ssg_cd_until = now + 50; // ~3 ticks @64 : enchaine les apex/kills
            g_last_hc = 100.f;
            g_want_silent = true;
            return true;
        }

        // === Fusils : micro-stop invisible -> tir SEULEMENT si vitesse ~0 et HC OK ===
        // Pas de spam : si on bouge, on stop 1-2 ticks puis on attend d'etre arrete.
        const float stop_spd = 15.f;

        if (!on_ground) {
            if (cfg::combat::ragebot_air_apex && !apex) {
                release_forced_move();
                g_stop_ticks = 0;
                return false;
            }
            if (cfg::combat::ragebot_autostop && spd > 20.f)
                begin_stop(1);
        } else {
            if (cfg::combat::ragebot_autostop && spd > stop_spd) {
                begin_stop(2);
                ragebot_autostop_tick();
                return false; // jamais tirer en courant
            }
            if (g_stop_ticks > 0) {
                ragebot_autostop_tick();
                if (spd > stop_spd)
                    return false;
            } else {
                release_forced_move();
            }
        }

        game::force_no_spread(pawn);

        float need = cfg::combat::ragebot_hitchance;
        if (!on_ground)
            need = (std::min)(need, cfg::combat::ragebot_hitchance_air);
        // Arrete = un peu plus permissif, mais PAS un spam a 15%.
        if (on_ground && spd <= stop_spd)
            need = (std::min)(need, 40.f);

        const float hc = ragebot_hitchance(aim, target_pawn);
        if (hc + 0.01f < need)
            return false;

        if (!cfg::combat::ragebot_autoshoot && !key_down(VK_LBUTTON))
            return false;

        // Un seul tir (pas multitap spam).
        write_attack(true);
        g_forced_atk = true;
        g_attack_hold = 1;
        g_multitap_left = 0;
        g_want_silent = true;
        return true;
    }

    bool ragebot_wants_silent() {
        return g_want_silent;
    }

    void ragebot_on_silent(bool did_silent) {
        if (!ragebot_on()) {
            g_multitap_left = 0;
            g_stop_ticks = 0;
            g_holding_stop = false;
            release_forced_attack();
            release_forced_move();
            return;
        }

        if (g_attack_hold > 0) {
            write_attack(true);
            g_forced_atk = true;
            --g_attack_hold;
            if (did_silent && g_multitap_left > 0)
                --g_multitap_left;
            if (g_stop_ticks > 0)
                ragebot_autostop_tick();
            return;
        }

        if (g_multitap_left > 0) {
            press_attack_hold(2);
            --g_multitap_left;
            return;
        }

        if (g_forced_atk && !key_down(VK_LBUTTON)) {
            write_attack(false);
            g_forced_atk = false;
        }
    }

    void ragebot_idle() {
        track_vz(game::local_pawn());
        if (g_attack_hold > 0 || g_multitap_left > 0) {
            write_attack(true);
            g_forced_atk = true;
            if (g_attack_hold > 0)
                --g_attack_hold;
            return;
        }
        g_stop_ticks = 0;
        g_holding_stop = false;
        release_forced_attack();
        release_forced_move();
        g_last_hc = 0.f;
        g_ssg_cd_until = 0; // rearme si plus de cible (ne bloque plus au retour)
    }

    void apply_rage_preset() {
        cfg::combat::ragebot = true;
        cfg::combat::ragebot_autoshoot = true;
        cfg::combat::ragebot_autostop = true;
        cfg::combat::ragebot_early_stop = false;
        cfg::combat::ragebot_air_apex = true;
        cfg::combat::ragebot_hitchance = 55.f;
        cfg::combat::ragebot_hitchance_air = 35.f;
        cfg::combat::ragebot_multitap = false;
        cfg::combat::ragebot_multitap_count = 1;
        cfg::combat::ragebot_antiaim = false;

        cfg::combat::silent = true;
        cfg::combat::silent_360 = true; // rage = 360 permanent
        cfg::combat::rage = false;
        cfg::combat::spinbot = false;

        cfg::combat::bhop = true;
        cfg::combat::airstrafe = true;
        cfg::combat::fast_stop = true;
        cfg::combat::ssg_jump = false;
        cfg::combat::autowall = true;
        cfg::combat::wall_check = false;
        cfg::combat::aim_key = 0;
    }
}
