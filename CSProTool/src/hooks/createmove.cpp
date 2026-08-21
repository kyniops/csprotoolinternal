#include "hooks/createmove.hpp"
#include "common.hpp"
#include "core/modules.hpp"
#include "core/memory.hpp"
#include "core/status.hpp"
#include "sdk/entity.hpp"
#include "sdk/usercmd.hpp"
#include "sdk/spread.hpp"
#include "features/target.hpp"
#include "features/silent.hpp"
#include "features/bhop.hpp"
#include "features/ssg.hpp"
#include "features/misc.hpp"
#include "features/trigger.hpp"
#include "features/ragebot.hpp"
#include "MinHook.h"
#include <Windows.h>
#include <fstream>
#include <cstdint>
#include <cmath>
#include <cstdarg>

namespace hooks {
    // CCSGOInput::CreateMove — vtable index 5. Signature large pour forwarder rcx/rdx/r8/r9.
    using CreateMoveFn = std::int64_t(__fastcall*)(void* thisptr, int slot, std::int64_t a3, std::int8_t a4);
    static CreateMoveFn o_create_move = nullptr;
    static bool hooked = false;
    static void* hooked_addr = nullptr;
    static std::atomic<uint32_t> g_ticks{ 0 };

    static void log_cm(const char* m) {
        std::ofstream f("C:\\Users\\Hugo\\Desktop\\csprotool_log.txt", std::ios::app);
        if (f) f << m << "\n";
    }

    static void logf(const char* fmt, ...) {
        char buf[256]{};
        va_list ap;
        va_start(ap, fmt);
        wvsprintfA(buf, fmt, ap);
        va_end(ap);
        log_cm(buf);
    }

    static void allow_cfg(void* fn) {
        if (!fn) return;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(fn, &mbi, sizeof(mbi))) return;
        using Fn = BOOL(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, CFG_CALL_TARGET_INFO*);
        const auto pSet = reinterpret_cast<Fn>(
            GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "SetProcessValidCallTargets"));
        if (!pSet) return;
        CFG_CALL_TARGET_INFO info{};
        info.Offset = static_cast<ULONG_PTR>(
            reinterpret_cast<uint8_t*>(fn) - static_cast<uint8_t*>(mbi.BaseAddress));
        info.Flags = CFG_CALL_TARGET_VALID;
        pSet(GetCurrentProcess(), mbi.BaseAddress, mbi.RegionSize, 1, &info);
    }

    static bool in_client(uintptr_t p) {
        const auto b = modules::client();
        return b && p > b && p < b + 0x08000000ull;
    }

    static bool looks_like_angle(float pitch, float yaw) {
        return std::isfinite(pitch) && std::isfinite(yaw)
            && pitch >= -89.5f && pitch <= 89.5f
            && yaw >= -180.5f && yaw <= 180.5f
            && (std::fabs(pitch) > 0.001f || std::fabs(yaw) > 0.001f);
    }

    static bool is_firing() {
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return true;
        const auto client = game::client_base();
        if (!client) return false;
        const int atk = mem::read<int>(client + schema::btn::attack);
        return atk == 65537 || (atk & 1) != 0;
    }

    static bool aim_key_down() {
        const int vk = cfg::combat::aim_key;
        if (vk <= 0) return true;
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    // Remplace toutes les copies pitch/yaw identiques dans CCSGOInput (history / cmd / viewangles).
    static int patch_angle_copies(uintptr_t input, const Vec3& from, const Vec3& to, size_t bytes) {
        int n = 0;
        if (!mem::valid(input) || bytes < 8) return 0;
        for (size_t i = 0x20; i + 8 <= bytes; i += 4) {
            float p = 0.f, y = 0.f;
            if (!mem::read_raw(input + i, &p, 4) || !mem::read_raw(input + i + 4, &y, 4))
                continue;
            if (!looks_like_angle(p, y)) continue;
            if (std::fabs(p - from.x) > 0.05f || std::fabs(y - from.y) > 0.05f)
                continue;
            mem::write<float>(input + i, to.x);
            mem::write<float>(input + i + 4, to.y);
            ++n;
        }
        return n;
    }

    static void write_history_slots(uintptr_t input, const Vec3& ang) {
        static constexpr uintptr_t k_count_off[] = { 0xBC8, 0xBB0, 0xC00, 0x548 };
        static constexpr uintptr_t k_array_off[] = { 0xBD0, 0xBB8, 0xC08, 0x550 };
        static constexpr uintptr_t k_pitch_off[] = { 0x10, 0x18 };
        static constexpr uintptr_t k_stride[] = { 0x60, 0x68 };

        for (int c = 0; c < 4; ++c) {
            const int count = mem::read<int>(input + k_count_off[c]);
            const auto arr = mem::read<uintptr_t>(input + k_array_off[c]);
            if (count < 1 || count > 64 || !mem::valid(arr) || in_client(arr))
                continue;
            bool wrote = false;
            for (int s = 0; s < 2; ++s) {
                for (int i = 0; i < count; ++i) {
                    const auto e = arr + k_stride[s] * static_cast<uintptr_t>(i);
                    if (!mem::valid(e)) continue;
                    const float p = mem::read<float>(e + k_pitch_off[s]);
                    const float y = mem::read<float>(e + k_pitch_off[s] + 4);
                    if (!looks_like_angle(p, y))
                        continue;
                    mem::write<float>(e + k_pitch_off[s], ang.x);
                    mem::write<float>(e + k_pitch_off[s] + 4, ang.y);
                    wrote = true;
                }
            }
            if (wrote)
                return;
        }
    }

    // Ne touche PAS m_angEyeAngles : l'ecrire chaque tir fait viser le modele (SSG twitch).
    static void restore_camera(void* thisptr, const Vec3& visual) {
        // Clamp pitch/yaw. Ne PAS ecrire dwViewAngles ici : ca se bat avec la souris
        // (surtout scope SSG) et fige la visee des que le ragebot tire en boucle.
        Vec3 v = visual;
        if (v.x > 89.f) v.x = 89.f;
        if (v.x < -89.f) v.x = -89.f;
        while (v.y > 180.f) v.y -= 360.f;
        while (v.y < -180.f) v.y += 360.f;
        const auto input = reinterpret_cast<uintptr_t>(thisptr);
        if (input)
            mem::write<Vec3>(input + schema::k_input_angles, v);
    }

    // Camera live SANS toucher roll (normalize_angles le mettait a 0 = nospread mort).
    static void write_live_view(void* thisptr, const Vec3& a) {
        Vec3 v = a;
        if (v.x > 89.f) v.x = 89.f;
        if (v.x < -89.f) v.x = -89.f;
        while (v.y > 180.f) v.y -= 360.f;
        while (v.y < -180.f) v.y += 360.f;
        const auto input = reinterpret_cast<uintptr_t>(thisptr);
        if (input)
            mem::write<Vec3>(input + schema::k_input_angles, v);
        const auto dump = game::client_base() + schema::off::client_dll::dwViewAngles;
        if (dump)
            mem::write<Vec3>(dump, v);
    }

    // Snap visible : camera + cmd.
    static void apply_snap(void* thisptr, const Vec3& aim, const Vec3& visual) {
        const auto input = reinterpret_cast<uintptr_t>(thisptr);
        Vec3 a = aim;
        normalize_angles(a);
        mem::write<Vec3>(input + schema::k_input_angles, a);
        game::set_view_angles(a);
        write_history_slots(input, a);
        patch_angle_copies(input, visual, a, 0x800);
    }

    // Silent : uniquement l'historique / cmd. Ne touche PAS la camera ni m_angEyeAngles.
    static void apply_silent(void* thisptr, const Vec3& aim, const Vec3& visual) {
        const auto input = reinterpret_cast<uintptr_t>(thisptr);
        Vec3 a = aim;
        normalize_angles(a);
        write_history_slots(input, a);
        int patched = 0;
        for (size_t i = 0x20; i + 8 <= 0xC00; i += 4) {
            if (i >= schema::k_input_angles && i < schema::k_input_angles + 12)
                continue;
            float p = 0.f, y = 0.f;
            if (!mem::read_raw(input + i, &p, 4) || !mem::read_raw(input + i + 4, &y, 4))
                continue;
            if (!looks_like_angle(p, y)) continue;
            if (std::fabs(p - visual.x) > 0.15f || std::fabs(y - visual.y) > 0.15f)
                continue;
            mem::write<float>(input + i, a.x);
            mem::write<float>(input + i + 4, a.y);
            ++patched;
        }
        (void)patched;
    }

    static float look_dist(const Vec3& a, const Vec3& b) {
        const float dp = a.x - b.x;
        float dy = a.y - b.y;
        while (dy > 180.f) dy -= 360.f;
        while (dy < -180.f) dy += 360.f;
        return std::sqrt(dp * dp + dy * dy);
    }

    static Vec3 g_user_look{};
    static bool g_user_look_ok = false;
    static Vec3 g_last_silent_aim{};
    static bool g_last_silent_aim_ok = false;
    static int g_restore_left = 0;
    static Vec3 g_free_look{};
    static bool g_free_look_ok = false;
    static bool g_silent_last_tick = false;
    static float g_spin_yaw = 0.f;

    float spinbot_yaw() {
        return g_spin_yaw;
    }

    static bool local_can_spin() {
        // AA / spinbot : desactive pour le focus Ragebot+Bhop (plus tard).
        if (!cfg::combat::spinbot) return false;
        if (!game::world_ready()) return false;
        const auto pawn = game::local_pawn();
        if (!mem::valid(pawn)) return false;
        if (mem::read<int>(pawn + schema::C_BaseEntity::m_iHealth) <= 0) return false;
        if (mem::read<uint8_t>(pawn + schema::C_BaseEntity::m_lifeState) != 0) return false;
        return true;
    }

    static Vec3 next_spin_ang(const Vec3& visual) {
        float spd = cfg::combat::spinbot_speed;
        if (spd < 20.f) spd = 20.f;
        if (spd > 360.f) spd = 360.f;
        static LARGE_INTEGER freq{};
        static LARGE_INTEGER prev{};
        static bool qpc_ok = false;
        if (!qpc_ok) {
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&prev);
            qpc_ok = true;
        }
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        float dt = (freq.QuadPart > 0)
            ? static_cast<float>(now.QuadPart - prev.QuadPart) / static_cast<float>(freq.QuadPart)
            : (1.f / 64.f);
        if (dt >= 0.001f) {
            prev = now;
            if (dt > 0.05f) dt = 0.05f;
            g_spin_yaw += spd * dt;
            while (g_spin_yaw > 180.f) g_spin_yaw -= 360.f;
            while (g_spin_yaw < -180.f) g_spin_yaw += 360.f;
        }
        Vec3 a = visual;
        a.y = g_spin_yaw;
        a.z = 0.f;
        if (a.x > 89.f) a.x = 89.f;
        if (a.x < -89.f) a.x = -89.f;
        return a;
    }

    static void send_net_angles(void* thisptr, std::int64_t a3, const Vec3& ang) {
        const auto input = reinterpret_cast<uintptr_t>(thisptr);
        write_history_slots(input, ang);
        usercmd::apply_silent_cmd(input, static_cast<uintptr_t>(a3), ang);
    }

    static std::int64_t __fastcall hk_create_move(void* thisptr, int slot, std::int64_t a3, std::int8_t a4) {
        g_ticks.fetch_add(1, std::memory_order_relaxed);
        tools::cm_ticks.store(g_ticks.load(std::memory_order_relaxed), std::memory_order_relaxed);

        if (!o_create_move) return 0;

        // CreateMove tourne encore pendant la destruction du monde: aucune feature
        // ne doit toucher aux entites a ce moment la.
        bool in_world = false;
        __try {
            in_world = game::world_ready();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (!in_world)
            return o_create_move(thisptr, slot, a3, a4);

        bool alive = false;
        __try {
            alive = game::local_alive();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (!alive)
            return o_create_move(thisptr, slot, a3, a4);

        __try {
            features::keep_scope_for_game();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        __try {
            features::run_ssg_tick();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        const bool ssg_spread = features::ssg_want_spread();

        __try {
            features::run_bhop_tick();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        // Ne pas ecrire dwViewAngles ici : avec SSG scope ca se bat avec la souris.
        // Ne jamais toucher les angles live pour "clear roll" : ca casse la visee SSG.
        // Le roll du laser reste uniquement dans le usercmd silent.

        __try {
            features::bhop_sync_cmd(thisptr, a3);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        __try {
            if (features::ssg_block_jump()) {
                const auto pawn = game::local_pawn();
                float fwd = 0.f, left = 0.f;
                game::wish_from_velocity(pawn, fwd, left);
                game::set_wish_move(pawn, fwd, left);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        // Ragebot = toujours silent 360 (meme en visant le sol/ciel).
        const bool rb = features::ragebot_on();
        const bool use_360 = cfg::combat::silent_360 || rb;
        const bool use_silent = cfg::combat::silent || use_360 || rb;
        const bool want_snap = (cfg::combat::aimbot || cfg::combat::rage) && !use_silent;
        const bool want = (use_silent || want_snap) && aim_key_down();

        Vec3 visual{};
        Vec3 aim{};
        uintptr_t aim_pawn = 0;
        bool have_target = false;
        bool did_silent = false;

        __try {
            if (thisptr)
                visual = mem::read<Vec3>(reinterpret_cast<uintptr_t>(thisptr) + schema::k_input_angles);

            // Toujours la souris de CE tick (deja appliquee par l'engine).
            g_user_look = visual;
            g_user_look_ok = true;
            if (!g_free_look_ok) {
                g_free_look = visual;
                g_free_look_ok = true;
            } else {
                const bool poisoned = g_last_silent_aim_ok
                    && look_dist(visual, g_last_silent_aim) < 2.0f
                    && look_dist(visual, g_free_look) > 2.0f;
                if (!poisoned)
                    g_free_look = visual;
            }

            const bool classic_silent = (cfg::combat::silent || cfg::combat::silent_360) && !rb;
            // Ragebot : scan meme si module SSG jump actif (sinon 0 cible = 0 tir).
            if (want && thisptr && (!ssg_spread || rb) && game::world_ready()) {
                if (rb) {
                    // 360 permanent : cibles derriere / hors ecran / regard ciel.
                    have_target = target::find_head(aim, &aim_pawn, true, false, false);
                } else {
                    const bool need_fire = classic_silent;
                    if (!need_fire || is_firing())
                        have_target = target::find_head(aim, &aim_pawn, use_360, false, !use_360);
                }
            }
            tools::last_aim.store(0, std::memory_order_relaxed);

            bool fire_ok = is_firing();
            bool want_silent_cmd = false;
            if (rb && have_target) {
                fire_ok = features::ragebot_allow_shot(aim, aim_pawn);
                want_silent_cmd = fire_ok && features::ragebot_wants_silent();
            } else if (rb) {
                features::ragebot_idle();
            } else if (have_target && classic_silent) {
                want_silent_cmd = fire_ok; // silent manuel : clic seulement
            }

            // NE PAS re-forcer silent pendant ragebot (sinon 1 tir puis bloque).
            if (!rb && classic_silent && have_target && is_firing())
                want_silent_cmd = true;

            if (have_target && thisptr) {
                if (use_silent && want_silent_cmd) {
                    const auto pawn = game::local_pawn();
                    const auto wpn = game::active_weapon(pawn);
                    Vec3 shoot = aim;
                    if (rb && mem::valid(wpn) && spread::item_index(wpn) == 40) {
                        game::force_no_spread(pawn);
                        Vec3 adj = aim;
                        if (!spread::straight_shot(aim, pawn, wpn, adj))
                            spread::roll_straight(aim, pawn, wpn, adj);
                        shoot = adj;
                    }
                    did_silent = true;
                    g_last_silent_aim = shoot;
                    g_last_silent_aim_ok = true;
                    g_restore_left = 0;
                    aim = shoot;
                    tools::last_aim.store(2, std::memory_order_relaxed);
                    // COMME AVANT : patch history CCSGOInput AVANT o_create_move.
                    apply_silent(thisptr, shoot, visual);
                    silent::set(shoot);
                    silent::shooting.store(true, std::memory_order_release);
                } else if (!use_silent && fire_ok) {
                    apply_snap(thisptr, aim, visual);
                    tools::last_aim.store(1, std::memory_order_relaxed);
                }
            }

            if (rb)
                features::ragebot_on_silent(did_silent);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        Vec3 ssg_adj{};
        bool ssg_did_roll = false;
        bool ssg_silent = false;
        __try {
            if (ssg_spread && thisptr) {
                const auto pawn = game::local_pawn();
                const auto wpn = game::active_weapon(pawn);
                Vec3 wish = visual;
                Vec3 stored{};
                if (features::ssg_wish_ang(stored))
                    wish = stored;
                Vec3 head{};
                if (game::world_ready() && target::find_ssg_head(head))
                    wish = head;
                features::ssg_mark_head(true);
                game::force_no_spread(pawn);
                ssg_adj = wish;
                if (!spread::straight_shot(wish, pawn, wpn, ssg_adj))
                    spread::roll_straight(wish, pawn, wpn, ssg_adj);

                ssg_silent = true; // jamais write_live_view / apply_silent : cam libre
                did_silent = true;
                g_last_silent_aim = ssg_adj;
                g_last_silent_aim_ok = true;
                g_restore_left = 0;
                // Silent SSG = usercmd seulement (apres o_create_move).
                ssg_did_roll = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        __try {
            features::run_triggerbot();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        const auto result = o_create_move(thisptr, slot, a3, a4);

        __try {
            features::bhop_sync_cmd(thisptr, a3);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        __try {
            if (features::ssg_block_jump()) {
                const auto pawn = game::local_pawn();
                float fwd = 0.f, left = 0.f;
                game::wish_from_velocity(pawn, fwd, left);
                game::set_wish_move(pawn, fwd, left);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        __try {
            features::ssg_flush_attack();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        __try {
            // === Archi Ragebot (CreateMove / UserCmd) ===
            // On modifie UNIQUEMENT le paquet de commande apres o_create_move.
            // Jamais k_input_angles / dwViewAngles / history CCSGOInput :
            // la camera souris reste celle du client (SSG scope libre).
            const auto input = thisptr ? reinterpret_cast<uintptr_t>(thisptr) : 0;
            const Vec3 cam = g_free_look_ok ? g_free_look : (g_user_look_ok ? g_user_look : visual);
            const bool rb_now = features::ragebot_on();

            bool ssg_held = ssg_silent || ssg_did_roll;
            (void)ssg_held;

            if ((did_silent || ssg_silent) && input) {
                const Vec3 shoot = ssg_did_roll ? ssg_adj : aim;
                // Comme avant : UserCmd complet (base + history) = hits.
                usercmd::apply_silent_cmd(input, static_cast<uintptr_t>(a3), shoot);
                // Re-patch CCSGOInput apres o_create_move (le jeu ecrase sinon).
                // SSG : skip apply_silent (cam), usercmd suffit.
                if (!ssg_did_roll)
                    apply_silent(thisptr, shoot, visual);
                g_last_silent_aim = shoot;
                g_last_silent_aim_ok = true;
                g_silent_last_tick = true;
                // Cam = souris reelle de ce tick (pas free-look stale).
                restore_camera(thisptr, visual);
            } else if (!ssg_spread && thisptr && local_can_spin()) {
                if (is_firing()) {
                    send_net_angles(thisptr, a3, cam);
                } else {
                    send_net_angles(thisptr, a3, next_spin_ang(cam));
                }
                restore_camera(thisptr, cam);
            } else if (rb_now && thisptr && g_silent_last_tick && g_last_silent_aim_ok) {
                const Vec3 live = mem::read<Vec3>(
                    reinterpret_cast<uintptr_t>(thisptr) + schema::k_input_angles);
                if (look_dist(live, g_last_silent_aim) < 2.5f)
                    restore_camera(thisptr, g_free_look_ok ? g_free_look : cam);
                else
                    g_silent_last_tick = false;
            }

            // Ne pas tuer le silent classique chaque tick.
            // Poison silent : clear des le tick suivant (sinon FOV/cible collent).
            if (!did_silent && !ssg_silent) {
                g_last_silent_aim_ok = false;
                g_silent_last_tick = false;
            }
            if (!rb_now && !(cfg::combat::silent || cfg::combat::silent_360)) {
                g_silent_last_tick = false;
                g_last_silent_aim_ok = false;
                g_free_look_ok = false;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }

        g_restore_left = 0;

        silent::clear();
        silent::shooting.store(false, std::memory_order_release);
        return result;
    }

    static void* vtable_fn(uintptr_t obj, int index) {
        const auto vt = mem::read<uintptr_t>(obj);
        if (!in_client(vt)) return nullptr;
        const auto fn = mem::read<uintptr_t>(vt + static_cast<uintptr_t>(index) * 8ull);
        if (!in_client(fn)) return nullptr;
        return reinterpret_cast<void*>(fn);
    }

    bool createmove_active() { return hooked; }
    unsigned createmove_ticks() { return g_ticks.load(std::memory_order_relaxed); }

    bool init_createmove() {
        if (hooked) return true;

        const auto input = game::csgo_input();
        if (!mem::valid(input)) {
            log_cm("[!] CCSGOInput introuvable");
            return false;
        }

        void* addr = vtable_fn(input, 5);
        int used = 5;
        if (!addr) {
            addr = vtable_fn(input, 21);
            used = 21;
        }
        if (!addr) {
            static int misses = 0;
            if (misses < 3 || (misses % 15) == 0)
                log_cm("[!] CreateMove vtable introuvable");
            ++misses;
            return false;
        }

        allow_cfg(reinterpret_cast<void*>(&hk_create_move));
        const auto created = MH_CreateHook(addr, reinterpret_cast<void*>(&hk_create_move),
            reinterpret_cast<void**>(&o_create_move));
        if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED) {
            logf("[!] MH_CreateHook failed (%d)", (int)created);
            return false;
        }
        if (MH_EnableHook(addr) != MH_OK) {
            MH_RemoveHook(addr);
            log_cm("[!] MH_EnableHook failed");
            return false;
        }

        hooked_addr = addr;
        hooked = true;
        logf("[+] CreateMove vtable[%d] @ %p", used, addr);
        return true;
    }

    void shutdown_createmove() {
        if (!hooked) return;
        if (hooked_addr) {
            MH_DisableHook(hooked_addr);
            MH_RemoveHook(hooked_addr);
        }
        hooked = false;
        hooked_addr = nullptr;
        o_create_move = nullptr;
    }
}
