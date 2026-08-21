#include "features/misc.hpp"
#include "sdk/entity.hpp"
#include "sdk/spread.hpp"
#include "features/silent.hpp"
#include "hooks/createmove.hpp"
#include "core/pattern.hpp"
#include "core/status.hpp"
#include "MinHook.h"
#include <Windows.h>
#include <fstream>
#include <cmath>
#include <cstring>

namespace features {
    using UpdateCameraFn = void(__fastcall*)(void* thisptr, void* a2);
    using OverrideViewFn = void(__fastcall*)(void* rcx, void* setup);
    using GetViewmodelFn = void*(__fastcall*)(void* a1, float* offsets, float* fov);

    static UpdateCameraFn o_update_camera = nullptr;
    static OverrideViewFn o_override_view = nullptr;
    static GetViewmodelFn o_get_viewmodel = nullptr;

    static void* g_cam_addr = nullptr;
    static void* g_ov_addr = nullptr;
    static void* g_gvm_addr = nullptr;
    static bool g_cam_hooked = false;
    static bool g_ov_hooked = false;
    static bool g_gvm_hooked = false;
    static int g_hook_tries = 0;

    constexpr uintptr_t k_fov = 0x498;
    constexpr uintptr_t k_origin = 0x4A0;
    constexpr uintptr_t k_angles = 0x4B8;

    static void log_view(const char* msg) {
        std::ofstream f("C:\\Users\\Hugo\\Desktop\\csprotool_log.txt", std::ios::app);
        if (f) f << msg << "\n";
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

    static bool looks_like_fov(float v) {
        return std::isfinite(v) && v >= 20.f && v <= 170.f;
    }

    static bool looks_like_origin(const Vec3& o) {
        const float l = o.length();
        return std::isfinite(l) && l > 8.f && l < 100000.f;
    }

    static bool looks_like_angles(const Vec3& a) {
        return std::isfinite(a.x) && std::isfinite(a.y)
            && a.x >= -89.5f && a.x <= 89.5f
            && a.y >= -180.5f && a.y <= 180.5f;
    }

    static void angle_vectors(const Vec3& a, Vec3& f) {
        const float d = 3.14159265f / 180.f;
        const float sp = std::sin(a.x * d), cp = std::cos(a.x * d);
        const float sy = std::sin(a.y * d), cy = std::cos(a.y * d);
        f = { cp * cy, cp * sy, -sp };
    }

    static float clamp_fov(float v) {
        if (v < 60.f) v = 60.f;
        if (v > 160.f) v = 160.f;
        return v;
    }

    static bool g_hid_overlay = false;
    static bool g_draw_scope_cross = false;
    static float g_scope_fov = 40.f;
    static uintptr_t g_arms = 0;
    static uint8_t g_arms_mode = 0;
    static uint32_t g_arms_fx = 0;
    static bool g_arms_saved = false;

    static bool sniper_zoomed() {
        const auto pawn = game::local_pawn();
        if (!mem::valid(pawn)) return false;
        const auto wpn = game::active_weapon(pawn);
        if (!mem::valid(wpn) || !spread::is_sniper(wpn))
            return false;
        const int zoom = mem::read<int>(wpn + schema::C_CSWeaponBaseGun::m_zoomLevel);
        if (zoom >= 1 && zoom <= 2)
            return true;
        const int mode = mem::read<int>(wpn + schema::C_CSWeaponBase::m_weaponMode);
        if (mode == 1)
            return true;
        return mem::read<uint8_t>(pawn + schema::C_CSPlayerPawn::m_bIsScoped) != 0;
    }

    static void restore_hud_arms() {
        if (!g_arms_saved) return;
        // Hors partie l'entite bras est deja liberee: on oublie sans ecrire.
        if (!game::world_ready()) {
            g_arms_saved = false;
            g_arms = 0;
            return;
        }
        if (mem::valid(g_arms)) {
            mem::write<uint8_t>(g_arms + schema::C_BaseModelEntity::m_nRenderMode, g_arms_mode);
            mem::write<uint32_t>(g_arms + schema::C_BaseEntity::m_fEffects, g_arms_fx);
        }
        g_arms_saved = false;
        g_arms = 0;
    }

    static void hide_hud_arms(uintptr_t pawn) {
        const auto handle = mem::read<uint32_t>(pawn + schema::C_CSPlayerPawn::m_hHudModelArms);
        if (!handle || handle == 0xFFFFFFFFu) return;
        const auto arms = game::get_entity(handle);
        if (!mem::valid(arms)) return;
        if (!g_arms_saved || g_arms != arms) {
            g_arms = arms;
            g_arms_mode = mem::read<uint8_t>(arms + schema::C_BaseModelEntity::m_nRenderMode);
            g_arms_fx = mem::read<uint32_t>(arms + schema::C_BaseEntity::m_fEffects);
            g_arms_saved = true;
        }
        mem::write<uint8_t>(arms + schema::C_BaseModelEntity::m_nRenderMode, 10);
        mem::write<uint32_t>(arms + schema::C_BaseEntity::m_fEffects, g_arms_fx | 0x20u);
    }

    void keep_scope_for_game() {
        __try {
            if (!game::local_alive() || !sniper_zoomed()) {
                restore_hud_arms();
                g_hid_overlay = false;
                g_draw_scope_cross = false;
                return;
            }
            if (!cfg::visuals::no_scope && !g_hid_overlay)
                return;
            const auto pawn = game::local_pawn();
            if (!mem::valid(pawn)) return;
            mem::write<uint8_t>(pawn + schema::C_CSPlayerPawn::m_bIsScoped, 1);
            mem::write<uint8_t>(pawn + schema::C_CSPlayerPawn::m_bOldIsScoped, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    static void strip_scope_overlay(void* setup) {
        if (!game::local_alive()) {
            restore_hud_arms();
            g_hid_overlay = false;
            g_draw_scope_cross = false;
            return;
        }
        if (!cfg::visuals::no_scope || !sniper_zoomed()) {
            restore_hud_arms();
            g_hid_overlay = false;
            g_draw_scope_cross = false;
            return;
        }
        const auto pawn = game::local_pawn();
        if (!mem::valid(pawn)) return;

        if (setup) {
            const auto base = reinterpret_cast<uintptr_t>(setup);
            float fov = 0.f;
            if (mem::read_raw(base + k_fov, &fov, 4) && looks_like_fov(fov)) {
                if (fov < 50.f)
                    g_scope_fov = fov;
                else if (g_scope_fov >= 10.f && g_scope_fov < 50.f)
                    mem::write<float>(base + k_fov, g_scope_fov);
            }
        }

        mem::write<uint8_t>(pawn + schema::C_CSPlayerPawn::m_bIsScoped, 0);
        mem::write<uint8_t>(pawn + schema::C_CSPlayerPawn::m_bOldIsScoped, 0);
        hide_hud_arms(pawn);
        g_hid_overlay = true;
        g_draw_scope_cross = true;
    }

    bool no_scope_crosshair() {
        return cfg::visuals::no_scope && g_draw_scope_cross;
    }

    static void apply_view_setup(void* setup) {
        if (!setup) return;
        const auto base = reinterpret_cast<uintptr_t>(setup);

        float fov = 0.f;
        if (!mem::read_raw(base + k_fov, &fov, 4) || !looks_like_fov(fov))
            return;

        const auto local = game::local_pawn();
        const bool scoped = (fov < 50.f)
            || (mem::valid(local) && mem::read<uint8_t>(local + schema::C_CSPlayerPawn::m_bIsScoped));

        if (cfg::visuals::player_fov && !scoped)
            mem::write<float>(base + k_fov, clamp_fov(cfg::visuals::player_fov_value));

        Vec3 ang_roll{};
        if (mem::read_raw(base + k_angles, &ang_roll, sizeof(ang_roll))
            && std::isfinite(ang_roll.z) && std::fabs(ang_roll.z) > 0.05f) {
            ang_roll.z = 0.f;
            mem::write<Vec3>(base + k_angles, ang_roll);
        }

        if (!cfg::visuals::third_person || scoped)
            return;

        Vec3 origin{}, angles{};
        if (!mem::read_raw(base + k_origin, &origin, sizeof(origin)))
            return;
        if (!mem::read_raw(base + k_angles, &angles, sizeof(angles)))
            return;
        if (!looks_like_origin(origin) || !looks_like_angles(angles))
            return;
        if (std::fabs(angles.z) > 0.05f) {
            angles.z = 0.f;
            mem::write<Vec3>(base + k_angles, angles);
        }

        Vec3 forward{};
        angle_vectors(angles, forward);
        float dist = cfg::visuals::third_person_dist;
        if (dist < 40.f) dist = 40.f;
        if (dist > 250.f) dist = 250.f;
        origin = origin - forward * dist;
        mem::write<Vec3>(base + k_origin, origin);
    }

    // Preview modele desactivee : ecrire m_angEyeAngles vole la visee (SSG).
    static void apply_local_spin_model() {
        (void)0;
    }

    static void __fastcall hk_override_view(void* rcx, void* setup) {
        __try {
            keep_scope_for_game();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (o_override_view)
            o_override_view(rcx, setup);
        __try {
            // Ne JAMAIS reecrire les angles view ici : ca fige la cam (surtout SSG).
            apply_view_setup(setup);
            strip_scope_overlay(setup);
            apply_local_spin_model();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    static void* __fastcall hk_get_viewmodel(void* a1, float* offsets, float* fov) {
        void* ret = nullptr;
        __try {
            if (o_get_viewmodel)
                ret = o_get_viewmodel(a1, offsets, fov);
            if (cfg::visuals::player_fov && fov) {
                const auto p = reinterpret_cast<uintptr_t>(fov);
                if (mem::valid(p))
                    mem::write<float>(p, clamp_fov(cfg::visuals::player_fov_value));
            }
            if (cfg::visuals::no_scope && g_draw_scope_cross && offsets) {
                const auto p = reinterpret_cast<uintptr_t>(offsets);
                if (mem::valid(p)) {
                    mem::write<float>(p + 0, 0.f);
                    mem::write<float>(p + 4, 48.f);
                    mem::write<float>(p + 8, -64.f);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return ret;
    }

    static void apply_third_person_state(void* thisptr) {
        if (!thisptr) return;
        if (!game::local_alive()) return;
        const auto input = reinterpret_cast<uintptr_t>(thisptr);
        if (cfg::visuals::third_person) {
            mem::write<uint8_t>(input + schema::k_in_thirdperson, 1);
            mem::write<float>(input + schema::k_thirdperson_angles + 8, cfg::visuals::third_person_dist);
        } else {
            mem::write<uint8_t>(input + schema::k_in_thirdperson, 0);
        }
    }

    static void __fastcall hk_update_camera(void* thisptr, void* a2) {
        __try {
            if (cfg::visuals::third_person)
                apply_third_person_state(thisptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        if (o_update_camera)
            o_update_camera(thisptr, a2);
        __try {
            if (cfg::visuals::third_person)
                apply_third_person_state(thisptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    static bool hook_fn(const char* pat, void* detour, void** orig, void** stored) {
        const auto addr = pattern::scan(L"client.dll", pat);
        if (!addr)
            return false;
        allow_cfg(detour);
        const auto created = MH_CreateHook(
            reinterpret_cast<void*>(addr), detour, orig);
        if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED)
            return false;
        if (MH_EnableHook(reinterpret_cast<void*>(addr)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void*>(addr));
            return false;
        }
        *stored = reinterpret_cast<void*>(addr);
        return true;
    }

    static void ensure_view_hooks() {
        if (g_ov_hooked && g_gvm_hooked)
            return;
        if (g_hook_tries > 25)
            return;
        const DWORD now = GetTickCount();
        static DWORD last_try = 0;
        if (g_hook_tries > 0 && now - last_try < 400)
            return;
        last_try = now;
        ++g_hook_tries;

        if (!g_ov_hooked) {
            if (hook_fn("40 57 48 83 EC ? 48 8B FA E8 ? ? ? ? BA",
                    reinterpret_cast<void*>(&hk_override_view),
                    reinterpret_cast<void**>(&o_override_view),
                    &g_ov_addr)) {
                g_ov_hooked = true;
                log_view("[+] OverrideView hooked (FOV + 3e personne)");
            }
        }

        if (!g_gvm_hooked && (cfg::visuals::player_fov || cfg::visuals::no_scope)) {
            const char* pats[] = {
                "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC ? 49 8B E8 48 8B DA 48 8B F1",
                "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 49 8B E8 48 8B DA 48 8B F1",
            };
            for (auto* pat : pats) {
                if (hook_fn(pat,
                        reinterpret_cast<void*>(&hk_get_viewmodel),
                        reinterpret_cast<void**>(&o_get_viewmodel),
                        &g_gvm_addr)) {
                    g_gvm_hooked = true;
                    log_view("[+] GetViewmodel hooked (FOV armes/mains)");
                    break;
                }
            }
        }

        if (!g_cam_hooked && cfg::visuals::third_person) {
            if (hook_fn("40 55 53 56 57 41 55 48 8D AC 24 ?",
                    reinterpret_cast<void*>(&hk_update_camera),
                    reinterpret_cast<void**>(&o_update_camera),
                    &g_cam_addr)) {
                g_cam_hooked = true;
                log_view("[+] UpdateCamera hooked");
            }
        }
    }

    void run_fov_changer() {
        ensure_view_hooks();

        const auto client = game::client_base();
        if (!client) return;

        const auto ctrl = mem::read<uintptr_t>(
            client + schema::off::client_dll::dwLocalPlayerController);
        if (!mem::valid(ctrl)) return;

        float fov = 90.f;
        float vm = 68.f;
        if (cfg::visuals::player_fov) {
            fov = clamp_fov(cfg::visuals::player_fov_value);
            vm = fov;
        }

        // Si OverrideView tourne, ne pas ecrire DesiredFOV (decale l'ESP).
        if (!g_ov_hooked) {
        mem::write<uint32_t>(
            ctrl + schema::CBasePlayerController::m_iDesiredFOV,
            static_cast<uint32_t>(fov));
        }

        const auto local = game::local_pawn();
        if (!mem::valid(local)) return;
            mem::write<float>(local + schema::C_CSPlayerPawn::m_flViewmodelFOV, vm);
    }

    void run_third_person() {
        ensure_view_hooks();
        const auto input = game::csgo_input();
        if (!input) return;
        apply_third_person_state(reinterpret_cast<void*>(input));
    }

    static void unhook(void*& addr) {
        if (!addr) return;
        MH_DisableHook(addr);
        MH_RemoveHook(addr);
        addr = nullptr;
    }

    void shutdown_third_person() {
        restore_hud_arms();
        g_hid_overlay = false;
        g_draw_scope_cross = false;
        unhook(g_ov_addr);
        unhook(g_gvm_addr);
        unhook(g_cam_addr);
        g_ov_hooked = g_gvm_hooked = g_cam_hooked = false;
        o_override_view = nullptr;
        o_get_viewmodel = nullptr;
        o_update_camera = nullptr;
    }

    enum class EnvKind : uint8_t { Sky, GradFog, FogCtrl, Tonemap, Post, Vis, CubeFog, VolFog };

    struct EnvEnt {
        uintptr_t p{};
        EnvKind kind{};
        char name[40]{};
        bool by_name = false;
        uint8_t tint[4]{};
        uint8_t tint_lit[4]{};
        uint8_t fog_col[4]{};
        float brightness = 1.f;
        float fog_str = 0.f;
        float fog_op = 0.f;
        float exp_min = 0.f;
        float exp_max = 0.f;
        float exp_comp = 0.f;
        bool enabled = false;
        bool saved = false;
    };

    static EnvEnt g_env[64];
    static int g_env_n = 0;
    static bool g_sky_take_all = false;
    static int g_env_scan = 0;
    static bool g_env_was_on = false;
    static bool g_atm_logged = false;

    struct CamFogBackup {
        uint8_t col[4]{};
        uint8_t col2[4]{};
        float start = 0.f;
        float end = 0.f;
        float dens = 0.f;
        float sky = 0.f;
        float scatter = 0.f;
        float hdr = 1.f;
        float light = 1.f;
        uint8_t enable = 0;
        bool saved = false;
    };
    static CamFogBackup g_cam_fog{};
    static CamFogBackup g_sky3d_fog{};

    struct ToneBackup {
        uintptr_t p{};
        float min = 1.f;
        float max = 1.f;
        float up = 1.f;
        float down = 1.f;
        float ev = 1.f;
        bool saved = false;
    };
    static ToneBackup g_tone{};

    struct PostBackup {
        uintptr_t p{};
        float min = 1.f;
        float max = 1.f;
        float comp = 0.f;
        float minlog = 0.f;
        float maxlog = 0.f;
        uint8_t ctrl = 0;
        bool saved = false;
    };
    static PostBackup g_post{};

    static void pack_color(uint8_t out[4], float r, float g, float b, float a = 1.f) {
        auto ch = [](float v) -> uint8_t {
            if (v < 0.f) v = 0.f;
            if (v > 1.f) v = 1.f;
            return static_cast<uint8_t>(v * 255.f + 0.5f);
        };
        out[0] = ch(r);
        out[1] = ch(g);
        out[2] = ch(b);
        out[3] = ch(a);
    }

    static bool icontains(const char* hay, const char* needle) {
        if (!hay || !needle || !needle[0]) return false;
        const size_t n = std::strlen(needle);
        for (const char* p = hay; *p; ++p) {
            size_t i = 0;
            for (; i < n && p[i]; ++i) {
                char a = p[i], b = needle[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                if (a != b) break;
            }
            if (i == n) return true;
        }
        return false;
    }

    static bool read_cstr(uintptr_t p, char* out, size_t n) {
        p &= ~7ull;
        if (!mem::valid(p) || !out || n < 4) return false;
        char tmp[80]{};
        if (!mem::read_raw(p, tmp, sizeof(tmp) - 1)) return false;
        if (tmp[0] < 'A' || tmp[0] > 'z') return false;
        size_t i = 0;
        for (; i + 1 < n && tmp[i]; ++i) {
            if (tmp[i] < 32) return false;
            out[i] = tmp[i];
        }
        out[i] = 0;
        return i >= 3;
    }

    // Le pointeur de nom n'est pas toujours aligne 8: on tente brut puis masque,
    // sinon les entites d'environnement deviennent invisibles au scan.
    static bool atm_get_name(uintptr_t ent, char* out, size_t n) {
        if (!mem::valid(ent) || !out || n < 4) return false;
        out[0] = 0;
        const auto ident = mem::read<uintptr_t>(ent + schema::CEntityInstance::m_pEntity);
        if (!mem::valid(ident)) return false;
        for (const auto off : {
            schema::CEntityIdentity::m_designerName,
            schema::CEntityIdentity::m_name }) {
            const auto raw = mem::read<uintptr_t>(ident + off);
            for (const auto p : { raw, raw & ~7ull }) {
                if (!mem::valid(p)) continue;
                char tmp[72]{};
                if (!mem::read_raw(p, tmp, sizeof(tmp) - 1)) continue;
                if (tmp[0] < ' ' || tmp[0] > 'z') continue;
                bool ok = true;
                for (int i = 0; i < 71 && tmp[i]; ++i) {
                    if (tmp[i] < 32) { ok = false; break; }
                }
                if (!ok) continue;
                size_t i = 0;
                for (; i + 1 < n && tmp[i]; ++i)
                    out[i] = tmp[i];
                out[i] = 0;
                if (i >= 3) return true;
            }
        }
        out[0] = 0;
        return false;
    }

    static bool entity_label(uintptr_t ent, char* out, size_t n) {
        if (atm_get_name(ent, out, n))
            return true;
        if (game::designer_name(ent, out, n))
            return true;
        const auto ident = mem::read<uintptr_t>(ent + schema::CEntityInstance::m_pEntity);
        if (!mem::valid(ident)) return false;
        const auto klass = mem::read<uintptr_t>(ident + 0x8);
        if (!mem::valid(klass)) return false;
        static constexpr uintptr_t k_off[] = { 0x8, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38 };
        for (auto off : k_off) {
            const auto sp = mem::read<uintptr_t>(klass + off);
            if (read_cstr(sp, out, n) && (icontains(out, "C_") || icontains(out, "env") || icontains(out, "sky")))
                return true;
        }
        return false;
    }

    // Un pawn joueur est assez gros pour avoir des donnees "plausibles" aux offsets
    // de C_EnvSky (0xFB0+): sans ce filtre on ecrit la teinte du ciel dans les bots.
    static bool looks_like_pawn(uintptr_t ent) {
        const int hp = mem::read<int>(ent + schema::C_BaseEntity::m_iHealth);
        const uint8_t team = mem::read<uint8_t>(ent + schema::C_BaseEntity::m_iTeamNum);
        if (hp > 0 && hp <= 100)
            return true;
        return team == 2 || team == 3;
    }

    static bool looks_like_sky(uintptr_t ent) {
        if (!mem::valid(ent))
            return false;
        if (looks_like_pawn(ent))
            return false;
        const auto handle = mem::read<uintptr_t>(ent + schema::C_EnvSky::m_hSkyMaterial);
        const float bright = mem::read<float>(ent + schema::C_EnvSky::m_flBrightnessScale);
        const uint8_t start_dis = mem::read<uint8_t>(ent + schema::C_EnvSky::m_bStartDisabled);
        const uint8_t enabled = mem::read<uint8_t>(ent + schema::C_EnvSky::m_bEnabled);
        const int fog_type = mem::read<int>(ent + schema::C_EnvSky::m_nFogType);
        if (start_dis > 1 || enabled > 1 || fog_type < 0 || fog_type > 3)
            return false;
        if (!std::isfinite(bright) || bright < 0.001f || bright > 80.f)
            return false;
        return mem::valid(handle);
    }

    static bool push_env(uintptr_t ent, EnvKind k, const char* name, bool by_name) {
        if (!mem::valid(ent) || g_env_n >= 64) return false;
        for (int i = 0; i < g_env_n; ++i) {
            if (g_env[i].p == ent) return false;
        }
        EnvEnt e{};
        e.p = ent;
        e.kind = k;
        e.by_name = by_name;
        if (name) {
            size_t i = 0;
            for (; i + 1 < sizeof(e.name) && name[i]; ++i)
                e.name[i] = name[i];
            e.name[i] = 0;
        }
        g_env[g_env_n++] = e;
        return true;
    }

    // Seul env_sky: les post_processing_volume provoquaient du flicker en mode nuit.
    static EnvKind kind_from_name(const char* name) {
        if (icontains(name, "env_sky") || icontains(name, "C_EnvSky"))
            return EnvKind::Sky;
        return static_cast<EnvKind>(255);
    }

    static uintptr_t ent_from_ident(uintptr_t ident) {
        if (!mem::valid(ident)) return 0;
        const auto ent = mem::read<uintptr_t>(ident);
        if (!mem::valid(ent)) return 0;
        if (mem::read<uintptr_t>(ent + schema::CEntityInstance::m_pEntity) != ident)
            return 0;
        return ent;
    }

    static void consider_ent(uintptr_t ent) {
        if (!mem::valid(ent)) return;
        char name[80]{};
        if (entity_label(ent, name, sizeof(name))) {
            const auto k = kind_from_name(name);
            if (static_cast<uint8_t>(k) != 255)
                push_env(ent, k, name, true);
            // Entite identifiee mais hors environnement: surtout ne pas la passer
            // a l'heuristique, elle y matcherait comme ciel (pawns, armes...).
            return;
        }
        if (looks_like_sky(ent))
            push_env(ent, EnvKind::Sky, "?", false);
    }

    static void scan_env() {
        // Les sauvegardes doivent survivre au rescan: sinon on resauvegarde la
        // luminosite qu'on vient d'ecrire et elle est remultipliee a chaque scan,
        // jusqu'a saturer le ciel en blanc.
        static EnvEnt prev[64];
        const int prev_n = g_env_n;
        for (int i = 0; i < prev_n; ++i)
            prev[i] = g_env[i];

        g_env_n = 0;

        int highest = 8192;
        const auto sys = game::entity_list();
        if (mem::valid(sys)) {
            const int h = mem::read<int>(sys + schema::off::client_dll::dwGameEntitySystem_highestEntityIndex);
            if (h > 64 && h < 0x8000)
                highest = h + 8;
        }
        if (highest > 8192)
            highest = 8192;
        for (int i = 1; i < highest && g_env_n < 64; ++i)
            consider_ent(game::get_entity(static_cast<uint32_t>(i)));

        // Les entites d'environnement client ne sont pas toutes joignables par index:
        // on remonte la liste chainee des identites depuis le pawn local.
        const auto pawn = game::local_pawn();
        if (mem::valid(pawn)) {
            auto ident = mem::read<uintptr_t>(pawn + schema::CEntityInstance::m_pEntity);
            if (mem::valid(ident) && mem::read<uintptr_t>(ident) == pawn) {
                for (int n = 0; n < 20000 && mem::valid(ident); ++n) {
                    const auto prev = mem::read<uintptr_t>(ident + schema::CEntityIdentity::m_pPrev);
                    if (!mem::valid(prev)) break;
                    ident = prev;
                }
                for (int n = 0; n < 20000 && mem::valid(ident) && g_env_n < 64; ++n) {
                    consider_ent(ent_from_ident(ident));
                    ident = mem::read<uintptr_t>(ident + schema::CEntityIdentity::m_pNext);
                }
            }
        }

        for (int i = 0; i < g_env_n; ++i) {
            for (int j = 0; j < prev_n; ++j) {
                if (!prev[j].saved || prev[j].p != g_env[i].p || prev[j].kind != g_env[i].kind)
                    continue;
                const auto p = g_env[i].p;
                const auto kind = g_env[i].kind;
                const bool by_name = g_env[i].by_name;
                char name[40]{};
                for (size_t c = 0; c < sizeof(name); ++c) name[c] = g_env[i].name[c];
                g_env[i] = prev[j];
                g_env[i].p = p;
                g_env[i].kind = kind;
                g_env[i].by_name = by_name;
                for (size_t c = 0; c < sizeof(name); ++c) g_env[i].name[c] = name[c];
                break;
            }
        }

        int skies = 0, named_skies = 0, fogs = 0;
        for (int i = 0; i < g_env_n; ++i) {
            if (g_env[i].kind == EnvKind::Sky) {
                ++skies;
                if (g_env[i].by_name) ++named_skies;
            } else {
                ++fogs;
            }
        }
        tools::atm_ents.store(g_env_n, std::memory_order_relaxed);

        static int last_logged = -1;
        if (g_env_n != last_logged) {
            last_logged = g_env_n;
            g_atm_logged = true;
            char line[160]{};
            wsprintfA(line, "[atm] %d ents (sky=%d dont nommes=%d, fog/pp=%d)",
                g_env_n, skies, named_skies, fogs);
            log_view(line);
            static const char* k_kind[] = {
                "SKY", "GRADFOG", "FOGCTRL", "TONEMAP", "POST", "VIS", "CUBEFOG", "VOLFOG" };
            for (int i = 0; i < g_env_n; ++i) {
                const auto ki = static_cast<uint8_t>(g_env[i].kind);
                wsprintfA(line, "[atm]   #%d %s %s \"%s\" @%p", i,
                    ki < 8 ? k_kind[ki] : "?",
                    g_env[i].by_name ? "nom" : "devine",
                    g_env[i].name,
                    reinterpret_cast<void*>(g_env[i].p));
                log_view(line);
            }
        }
    }

    static void save_env(EnvEnt& e) {
        if (e.saved || !mem::valid(e.p)) return;
        namespace Sky = schema::C_EnvSky;
        namespace Grad = schema::C_GradientFog;
        namespace FogC = schema::C_FogController;
        namespace Tone = schema::C_TonemapController2;
        namespace Post = schema::C_PostProcessingVolume;
        namespace Visb = schema::C_PlayerVisibility;
        namespace FogP = schema::fogparams_t;
        namespace Cube = schema::C_EnvCubemapFog;
        namespace Vol = schema::C_EnvVolumetricFogController;
        switch (e.kind) {
        case EnvKind::Sky:
            mem::read_raw(e.p + Sky::m_vTintColor, e.tint, 4);
            mem::read_raw(e.p + Sky::m_vTintColorLightingOnly, e.tint_lit, 4);
            e.brightness = mem::read<float>(e.p + Sky::m_flBrightnessScale);
            e.enabled = mem::read<uint8_t>(e.p + Sky::m_bEnabled) != 0;
            break;
        case EnvKind::GradFog:
            mem::read_raw(e.p + Grad::m_fogColor, e.fog_col, 4);
            e.fog_str = mem::read<float>(e.p + Grad::m_flFogStrength);
            e.fog_op = mem::read<float>(e.p + Grad::m_flFogMaxOpacity);
            e.enabled = mem::read<uint8_t>(e.p + Grad::m_bIsEnabled) != 0;
            break;
        case EnvKind::FogCtrl:
            mem::read_raw(e.p + FogC::m_fog + FogP::colorPrimary, e.fog_col, 4);
            e.fog_op = mem::read<float>(e.p + FogC::m_fog + FogP::maxdensity);
            e.enabled = mem::read<uint8_t>(e.p + FogC::m_fog + FogP::enable) != 0;
            break;
        case EnvKind::Tonemap:
            e.exp_min = mem::read<float>(e.p + Tone::m_flAutoExposureMin);
            e.exp_max = mem::read<float>(e.p + Tone::m_flAutoExposureMax);
            break;
        case EnvKind::Post:
            e.exp_min = mem::read<float>(e.p + Post::m_flMinExposure);
            e.exp_max = mem::read<float>(e.p + Post::m_flMaxExposure);
            e.exp_comp = mem::read<float>(e.p + Post::m_flExposureCompensation);
            e.enabled = mem::read<uint8_t>(e.p + Post::m_bExposureControl) != 0;
            break;
        case EnvKind::Vis:
            e.fog_str = mem::read<float>(e.p + Visb::m_flVisibilityStrength);
            e.fog_op = mem::read<float>(e.p + Visb::m_flFogMaxDensityMultiplier);
            e.enabled = mem::read<uint8_t>(e.p + Visb::m_bIsEnabled) != 0;
            break;
        case EnvKind::CubeFog:
            e.fog_op = mem::read<float>(e.p + Cube::m_flFogMaxOpacity);
            e.enabled = mem::read<uint8_t>(e.p + Cube::m_bActive) != 0;
            break;
        case EnvKind::VolFog:
            mem::read_raw(e.p + Vol::m_TintColor, e.fog_col, 4);
            e.fog_str = mem::read<float>(e.p + Vol::m_flScattering);
            e.enabled = mem::read<uint8_t>(e.p + Vol::m_bActive) != 0;
            break;
        }
        e.saved = true;
    }

    // Appel moteur qui pousse reellement la teinte dans le materiau du ciel.
    // Sans lui on ne modifie que le fog, la skybox elle-meme reste inchangee.
    using UpdateSkyboxFn = void(__fastcall*)(void* env_sky);
    static UpdateSkyboxFn g_update_skybox = nullptr;
    static int g_upd_tries = 0;
    static int g_upd_fails = 0;

    static void find_update_skybox() {
        if (g_update_skybox || g_upd_tries > 12)
            return;
        ++g_upd_tries;
        // Ordre repris de la v196: son pattern exact d'abord, les variantes ensuite.
        const char* pats[] = {
            "48 89 5C 24 10 48 89 74 24 18 55 57 41 54 41 55 41 57 48 8B EC 48 83 EC 70 48 83 B9 B0 0F 00 00",
            "48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 55 41 57 48 8B EC 48 83 EC ? 48 83 B9 B0 0F 00 00",
            "48 8B C4 48 89 58 ? 48 89 70 ? 55 57 41 54 41 55 41 57 48 8D 68 ? 48 81 EC ? ? ? ? 48 83 B9 B0 0F 00 00",
        };
        uintptr_t addr = 0;
        int hit = -1;
        for (int i = 0; i < static_cast<int>(sizeof(pats) / sizeof(pats[0])); ++i) {
            addr = pattern::scan(L"client.dll", pats[i]);
            if (addr) { hit = i; break; }
        }
        if (!addr) {
            tools::atm_upd.store(0, std::memory_order_relaxed);
            if (g_upd_tries == 1)
                log_view("[atm] UpdateSkybox introuvable");
            return;
        }
        g_update_skybox = reinterpret_cast<UpdateSkyboxFn>(addr);
        allow_cfg(reinterpret_cast<void*>(addr));
        tools::atm_upd.store(1, std::memory_order_relaxed);
        char line[96]{};
        wsprintfA(line, "[atm] UpdateSkybox %p (pattern %d)", reinterpret_cast<void*>(addr), hit);
        log_view(line);
    }

    static void call_update_skybox(uintptr_t ent) {
        if (!g_update_skybox || !mem::valid(ent))
            return;
        __try {
            g_update_skybox(reinterpret_cast<void*>(ent));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ++g_upd_fails;
            if (g_upd_fails >= 8) {
                g_update_skybox = nullptr;
                tools::atm_upd.store(0, std::memory_order_relaxed);
                log_view("[atm] UpdateSkybox crash — desactive");
            }
        }
    }

    static void restore_env(EnvEnt& e) {
        if (!e.saved || !mem::valid(e.p)) return;
        namespace Sky = schema::C_EnvSky;
        namespace Grad = schema::C_GradientFog;
        namespace FogC = schema::C_FogController;
        namespace Tone = schema::C_TonemapController2;
        namespace Post = schema::C_PostProcessingVolume;
        namespace Visb = schema::C_PlayerVisibility;
        namespace FogP = schema::fogparams_t;
        namespace Cube = schema::C_EnvCubemapFog;
        namespace Vol = schema::C_EnvVolumetricFogController;
        switch (e.kind) {
        case EnvKind::Sky:
            mem::write_raw(e.p + Sky::m_vTintColor, e.tint, 4);
            mem::write_raw(e.p + Sky::m_vTintColorLightingOnly, e.tint_lit, 4);
            mem::write<float>(e.p + Sky::m_flBrightnessScale, e.brightness);
            mem::write<uint8_t>(e.p + Sky::m_bEnabled, e.enabled ? 1 : 0);
            break;
        case EnvKind::GradFog:
            mem::write_raw(e.p + Grad::m_fogColor, e.fog_col, 4);
            mem::write<float>(e.p + Grad::m_flFogStrength, e.fog_str);
            mem::write<float>(e.p + Grad::m_flFogMaxOpacity, e.fog_op);
            mem::write<uint8_t>(e.p + Grad::m_bIsEnabled, e.enabled ? 1 : 0);
            break;
        case EnvKind::FogCtrl:
            mem::write_raw(e.p + FogC::m_fog + FogP::colorPrimary, e.fog_col, 4);
            mem::write<float>(e.p + FogC::m_fog + FogP::maxdensity, e.fog_op);
            mem::write<uint8_t>(e.p + FogC::m_fog + FogP::enable, e.enabled ? 1 : 0);
            break;
        case EnvKind::Tonemap:
            mem::write<float>(e.p + Tone::m_flAutoExposureMin, e.exp_min);
            mem::write<float>(e.p + Tone::m_flAutoExposureMax, e.exp_max);
            break;
        case EnvKind::Post:
            mem::write<float>(e.p + Post::m_flMinExposure, e.exp_min);
            mem::write<float>(e.p + Post::m_flMaxExposure, e.exp_max);
            mem::write<float>(e.p + Post::m_flExposureCompensation, e.exp_comp);
            mem::write<uint8_t>(e.p + Post::m_bExposureControl, e.enabled ? 1 : 0);
            break;
        case EnvKind::Vis:
            mem::write<float>(e.p + Visb::m_flVisibilityStrength, e.fog_str);
            mem::write<float>(e.p + Visb::m_flFogMaxDensityMultiplier, e.fog_op);
            mem::write<uint8_t>(e.p + Visb::m_bIsEnabled, e.enabled ? 1 : 0);
            break;
        case EnvKind::CubeFog:
            mem::write<float>(e.p + Cube::m_flFogMaxOpacity, e.fog_op);
            mem::write<uint8_t>(e.p + Cube::m_bActive, e.enabled ? 1 : 0);
            break;
        case EnvKind::VolFog:
            mem::write_raw(e.p + Vol::m_TintColor, e.fog_col, 4);
            mem::write<float>(e.p + Vol::m_flScattering, e.fog_str);
            mem::write<uint8_t>(e.p + Vol::m_bActive, e.enabled ? 1 : 0);
            break;
        }
    }

    static bool env_ent_ok(const EnvEnt& e) {
        if (!mem::valid(e.p)) return false;
        if (e.kind == EnvKind::Sky && !looks_like_sky(e.p))
            return false;
        return true;
    }

    static void apply_env(EnvEnt& e, const uint8_t col[4], float night, float fog) {
        if (!env_ent_ok(e)) {
            e.p = 0;
            return;
        }
        if (!mem::valid(e.p)) return;
        save_env(e);
        namespace Sky = schema::C_EnvSky;
        namespace Grad = schema::C_GradientFog;
        namespace FogC = schema::C_FogController;
        namespace Tone = schema::C_TonemapController2;
        namespace Post = schema::C_PostProcessingVolume;
        namespace Visb = schema::C_PlayerVisibility;
        namespace FogP = schema::fogparams_t;
        namespace Cube = schema::C_EnvCubemapFog;
        namespace Vol = schema::C_EnvVolumetricFogController;
        const float bright = 1.6f - night * 1.45f;
        float exp = 1.f - night * 0.9f;
        if (exp < 0.05f) exp = 0.05f;
        const float fog_op = 0.08f + fog * 0.72f;
        const float fog_str = 0.2f + fog * 1.6f;
        switch (e.kind) {
        case EnvKind::Sky:
            // Une map embarque plusieurs env_sky dont un seul est actif: on ne teinte
            // que celui-la, sinon les skybox s'empilent et decoupent l'ecran en noir.
            // Mais certaines maps n'en marquent aucun comme actif; dans ce cas on les
            // prend tous, sans jamais forcer m_bEnabled.
            if (!g_sky_take_all && mem::read<uint8_t>(e.p + Sky::m_bEnabled) == 0)
                break;
            mem::write_raw(e.p + Sky::m_vTintColor, col, 4);
            // m_vTintColorLightingOnly teinte l'eclairage ambiant que le ciel projette:
            // l'ecrire repeignait toute la map, pas seulement le ciel.
            // La map est assombrie par l'exposition, pas par le ciel: celui-ci ne
            // perd qu'un peu de luminosite pour que la teinte reste franche.
            {
                float inten = cfg::visuals::atmosphere_intensity / 100.f;
                if (inten < 0.1f) inten = 0.1f;
                // Plus la nuit monte, plus le ciel s'eteint (avant: seulement 35%).
                float b = e.brightness * inten * (1.f - night * 0.82f);
                if (!std::isfinite(b) || b < 0.02f) b = 0.02f;
                if (b > 8.f) b = 8.f;
                mem::write<float>(e.p + Sky::m_flBrightnessScale, b);
                // Teinte d'eclairage ambiant: c'est ce qui assombrissait la MAP
                // (pas seulement le ciel). Scale avec la nuit.
                if (night > 0.02f) {
                    const float dark = 1.f - night * 0.88f;
                    uint8_t lit[4] = {
                        static_cast<uint8_t>((std::max)(0, (int)(col[0] * dark * 0.45f))),
                        static_cast<uint8_t>((std::max)(0, (int)(col[1] * dark * 0.45f))),
                        static_cast<uint8_t>((std::max)(0, (int)(col[2] * dark * 0.45f))),
                        255
                    };
                    mem::write_raw(e.p + Sky::m_vTintColorLightingOnly, lit, 4);
                }
            }
            break;
        case EnvKind::GradFog:
            mem::write_raw(e.p + Grad::m_fogColor, col, 4);
            mem::write<float>(e.p + Grad::m_flFogStrength, fog_str);
            mem::write<float>(e.p + Grad::m_flFogMaxOpacity, fog_op);
            mem::write<float>(e.p + Grad::m_flFogStartDistance, 80.f);
            mem::write<float>(e.p + Grad::m_flFogEndDistance, 3500.f - fog * 2200.f);
            mem::write<uint8_t>(e.p + Grad::m_bIsEnabled, 1);
            mem::write<uint8_t>(e.p + Grad::m_bStartDisabled, 0);
            break;
        case EnvKind::FogCtrl:
            mem::write_raw(e.p + FogC::m_fog + FogP::colorPrimary, col, 4);
            mem::write_raw(e.p + FogC::m_fog + FogP::colorSecondary, col, 4);
            mem::write<float>(e.p + FogC::m_fog + FogP::maxdensity, fog_op);
            mem::write<float>(e.p + FogC::m_fog + FogP::start, 50.f);
            mem::write<float>(e.p + FogC::m_fog + FogP::end, 2800.f - fog * 1600.f);
            mem::write<uint8_t>(e.p + FogC::m_fog + FogP::enable, 1);
            break;
        case EnvKind::Tonemap:
            mem::write<float>(e.p + Tone::m_flAutoExposureMin, exp);
            mem::write<float>(e.p + Tone::m_flAutoExposureMax, exp + 0.15f);
            break;
        case EnvKind::Post:
            // Tous les volumes recoivent la meme exposition: en n'en forcant qu'un
            // seul, passer de l'un a l'autre faisait pomper la luminosite.
            if (night <= 0.02f) {
                restore_env(e);
                break;
            }
            mem::write<uint8_t>(e.p + Post::m_bExposureControl, 1);
            mem::write<float>(e.p + Post::m_flMinExposure, exp);
            mem::write<float>(e.p + Post::m_flMaxExposure, exp);
            mem::write<float>(e.p + Post::m_flExposureCompensation, -night * 1.6f);
            break;
        case EnvKind::Vis:
            mem::write<float>(e.p + Visb::m_flVisibilityStrength, 1.f);
            mem::write<float>(e.p + Visb::m_flFogMaxDensityMultiplier, 1.f + fog);
            mem::write<uint8_t>(e.p + Visb::m_bIsEnabled, 1);
            break;
        case EnvKind::CubeFog:
            mem::write<uint8_t>(e.p + Cube::m_bActive, fog > 0.05f ? 1 : 0);
            mem::write<float>(e.p + Cube::m_flFogMaxOpacity, fog_op);
            break;
        case EnvKind::VolFog:
            mem::write_raw(e.p + Vol::m_TintColor, col, 4);
            mem::write<float>(e.p + Vol::m_flScattering, 0.2f + fog * 1.4f);
            mem::write<uint8_t>(e.p + Vol::m_bActive, 1);
            mem::write<uint8_t>(e.p + Vol::m_bStartDisabled, 0);
            break;
        default:
            break;
        }
    }

    static void restore_fog_block(uintptr_t fp, const CamFogBackup& b) {
        if (!b.saved || !mem::valid(fp)) return;
        namespace FogP = schema::fogparams_t;
        mem::write_raw(fp + FogP::colorPrimary, b.col, 4);
        mem::write_raw(fp + FogP::colorSecondary, b.col2, 4);
        mem::write<float>(fp + FogP::start, b.start);
        mem::write<float>(fp + FogP::end, b.end);
        mem::write<float>(fp + FogP::maxdensity, b.dens);
        mem::write<float>(fp + FogP::skyboxFogFactor, b.sky);
        mem::write<float>(fp + FogP::scattering, b.scatter);
        mem::write<float>(fp + FogP::HDRColorScale, b.hdr);
        mem::write<float>(fp + FogP::locallightscale, b.light);
        mem::write<uint8_t>(fp + FogP::enable, b.enable);
    }

    static void restore_local_fog() {
        const auto pawn = game::local_pawn();
        if (!mem::valid(pawn)) return;
        namespace Cam = schema::CPlayer_CameraServices;
        namespace Sky3 = schema::sky3dparams_t;
        namespace PFog = schema::C_fogplayerparams_t;

        const auto cam = mem::read<uintptr_t>(pawn + schema::C_BasePlayerPawn::m_pCameraServices);
        if (mem::valid(cam)) {
            restore_fog_block(cam + Cam::m_CurrentFog, g_cam_fog);
            for (int i = 0; i < 5; ++i) {
                mem::write<uint8_t>(cam + Cam::m_bOverrideFogColor + i, 0);
                mem::write<uint8_t>(cam + Cam::m_bOverrideFogStartEnd + i, 0);
            }
            mem::write<float>(cam + Cam::m_PlayerFog + PFog::m_flTransitionTime, 0.f);
        }
        restore_fog_block(pawn + schema::C_BasePlayerPawn::m_skybox3d + Sky3::fog, g_sky3d_fog);
        g_cam_fog.saved = false;
        g_sky3d_fog.saved = false;
    }

    static void restore_camera_night() {
        namespace Tone = schema::C_TonemapController2;
        namespace Post = schema::C_PostProcessingVolume;
        if (g_tone.saved && mem::valid(g_tone.p)) {
            mem::write<float>(g_tone.p + Tone::m_flAutoExposureMin, g_tone.min);
            mem::write<float>(g_tone.p + Tone::m_flAutoExposureMax, g_tone.max);
            mem::write<float>(g_tone.p + Tone::m_flExposureAdaptationSpeedUp, g_tone.up);
            mem::write<float>(g_tone.p + Tone::m_flExposureAdaptationSpeedDown, g_tone.down);
            mem::write<float>(g_tone.p + Tone::m_flTonemapEVSmoothingRange, g_tone.ev);
        }
        g_tone.saved = false;
        g_tone.p = 0;
        if (g_post.saved && mem::valid(g_post.p)) {
            mem::write<float>(g_post.p + Post::m_flMinExposure, g_post.min);
            mem::write<float>(g_post.p + Post::m_flMaxExposure, g_post.max);
            mem::write<float>(g_post.p + Post::m_flExposureCompensation, g_post.comp);
            mem::write<float>(g_post.p + Post::m_flMinLogExposure, g_post.minlog);
            mem::write<float>(g_post.p + Post::m_flMaxLogExposure, g_post.maxlog);
            mem::write<uint8_t>(g_post.p + Post::m_bExposureControl, g_post.ctrl);
        }
        g_post.saved = false;
        g_post.p = 0;
    }

    static void apply_camera_night(float night) {
        if (night <= 0.02f) {
            restore_camera_night();
            return;
        }
        const auto pawn = game::local_pawn();
        if (!mem::valid(pawn)) return;
        namespace Cam = schema::CPlayer_CameraServices;
        namespace Tone = schema::C_TonemapController2;
        namespace Post = schema::C_PostProcessingVolume;
        const auto cam = mem::read<uintptr_t>(pawn + schema::C_BasePlayerPawn::m_pCameraServices);
        if (!mem::valid(cam)) return;

        // Courbe plus agressive qu'avant (plancher 0.04 au lieu de 0.16).
        float exp = 1.f - night * 0.96f;
        if (exp < 0.04f) exp = 0.04f;

        const auto h_tone = mem::read<uint32_t>(cam + Cam::m_hTonemapController);
        const auto tone = (h_tone && h_tone != 0xFFFFFFFFu) ? game::get_entity(h_tone) : 0;
        if (mem::valid(tone)) {
            if (g_tone.saved && g_tone.p && g_tone.p != tone) {
                // Restore ancien tonemap avant de basculer.
                mem::write<float>(g_tone.p + Tone::m_flAutoExposureMin, g_tone.min);
                mem::write<float>(g_tone.p + Tone::m_flAutoExposureMax, g_tone.max);
                mem::write<float>(g_tone.p + Tone::m_flExposureAdaptationSpeedUp, g_tone.up);
                mem::write<float>(g_tone.p + Tone::m_flExposureAdaptationSpeedDown, g_tone.down);
                mem::write<float>(g_tone.p + Tone::m_flTonemapEVSmoothingRange, g_tone.ev);
                g_tone.saved = false;
            }
            if (!g_tone.saved) {
                g_tone.p = tone;
                g_tone.min = mem::read<float>(tone + Tone::m_flAutoExposureMin);
                g_tone.max = mem::read<float>(tone + Tone::m_flAutoExposureMax);
                g_tone.up = mem::read<float>(tone + Tone::m_flExposureAdaptationSpeedUp);
                g_tone.down = mem::read<float>(tone + Tone::m_flExposureAdaptationSpeedDown);
                g_tone.ev = mem::read<float>(tone + Tone::m_flTonemapEVSmoothingRange);
                g_tone.saved = true;
            }
            mem::write<float>(tone + Tone::m_flAutoExposureMin, exp);
            mem::write<float>(tone + Tone::m_flAutoExposureMax, exp);
            mem::write<float>(tone + Tone::m_flExposureAdaptationSpeedUp, 80.f);
            mem::write<float>(tone + Tone::m_flExposureAdaptationSpeedDown, 80.f);
            mem::write<float>(tone + Tone::m_flTonemapEVSmoothingRange, 0.01f);
        }

        // Uniquement le volume ACTIF (pas tous) — evite le flicker en se deplacant,
        // mais retrouve l'assombrissement fort d'avant.
        const auto h_post = mem::read<uint32_t>(cam + Cam::m_hActivePostProcessingVolume);
        const auto post = (h_post && h_post != 0xFFFFFFFFu) ? game::get_entity(h_post) : 0;
        if (mem::valid(post)) {
            if (g_post.saved && g_post.p && g_post.p != post) {
                mem::write<float>(g_post.p + Post::m_flMinExposure, g_post.min);
                mem::write<float>(g_post.p + Post::m_flMaxExposure, g_post.max);
                mem::write<float>(g_post.p + Post::m_flExposureCompensation, g_post.comp);
                mem::write<float>(g_post.p + Post::m_flMinLogExposure, g_post.minlog);
                mem::write<float>(g_post.p + Post::m_flMaxLogExposure, g_post.maxlog);
                mem::write<uint8_t>(g_post.p + Post::m_bExposureControl, g_post.ctrl);
                g_post.saved = false;
            }
            if (!g_post.saved) {
                g_post.p = post;
                g_post.min = mem::read<float>(post + Post::m_flMinExposure);
                g_post.max = mem::read<float>(post + Post::m_flMaxExposure);
                g_post.comp = mem::read<float>(post + Post::m_flExposureCompensation);
                g_post.minlog = mem::read<float>(post + Post::m_flMinLogExposure);
                g_post.maxlog = mem::read<float>(post + Post::m_flMaxLogExposure);
                g_post.ctrl = mem::read<uint8_t>(post + Post::m_bExposureControl);
                g_post.saved = true;
            }
            mem::write<uint8_t>(post + Post::m_bExposureControl, 1);
            mem::write<float>(post + Post::m_flMinExposure, exp);
            mem::write<float>(post + Post::m_flMaxExposure, exp);
            mem::write<float>(post + Post::m_flExposureCompensation, -night * 2.4f);
            mem::write<float>(post + Post::m_flMinLogExposure, -8.f);
            mem::write<float>(post + Post::m_flMaxLogExposure, -1.f + (1.f - night) * 3.f);
        }
    }

    void run_atmosphere() {
        __try {
            const bool on = cfg::visuals::atmosphere && game::world_ready();
            uint8_t col[4]{};
            pack_color(col,
                cfg::visuals::atmosphere_color[0],
                cfg::visuals::atmosphere_color[1],
                cfg::visuals::atmosphere_color[2],
                1.f);
            float night = cfg::visuals::atmosphere_night / 100.f;
            if (night < 0.f) night = 0.f;
            if (night > 1.f) night = 1.f;
            const float fog = 0.f;

            if (!on) {
                if (g_env_was_on) {
                    if (game::world_ready()) {
                        for (int i = 0; i < g_env_n; ++i)
                            __try { restore_env(g_env[i]); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                        __try { restore_local_fog(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                        __try { restore_camera_night(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    }
                    g_env_n = 0;
                    g_cam_fog.saved = false;
                    g_sky3d_fog.saved = false;
                    g_tone.saved = false;
                    g_post.saved = false;
                    g_env_was_on = false;
                    tools::atm_ents.store(0, std::memory_order_relaxed);
                }
                return;
            }

            // Un scan vide ne doit pas relancer un balayage complet a chaque frame.
            if (++g_env_scan >= (g_env_n == 0 ? 15 : 45)) {
                g_env_scan = 0;
                __try {
                    scan_env();
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    g_env_n = 0;
                }
            }

            find_update_skybox();

            int sky_n = 0, sky_on = 0;
            for (int i = 0; i < g_env_n; ++i) {
                if (g_env[i].kind != EnvKind::Sky) continue;
                ++sky_n;
                if (mem::read<uint8_t>(g_env[i].p + schema::C_EnvSky::m_bEnabled) != 0)
                    ++sky_on;
            }
            g_sky_take_all = sky_on == 0;
            {
                static int last_on = -1, last_n = -1;
                if (sky_on != last_on || sky_n != last_n) {
                    last_on = sky_on;
                    last_n = sky_n;
                    char line[96]{};
                    wsprintfA(line, "[atm] ciels %d actifs sur %d%s",
                        sky_on, sky_n, g_sky_take_all ? " -> tous pris" : "");
                    log_view(line);
                }
            }

            // Le brouillard du joueur n'est plus touche du tout: forcer fogparams
            // repeignait la map et faisait apparaitre le bas noir de la skybox.
            for (int i = 0; i < g_env_n; ++i) {
                if (!env_ent_ok(g_env[i])) {
                    g_env[i].p = 0;
                    continue;
                }
                apply_env(g_env[i], col, night, fog);
            }
            apply_camera_night(night);

            // La teinte n'est lue par le moteur qu'au rebuild de la scene du ciel:
            // comme en v196 on repousse l'appel a chaque frame, sinon le rendu
            // revient a la skybox d'origine.
            for (int i = 0; i < g_env_n; ++i) {
                if (g_env[i].kind != EnvKind::Sky)
                    continue;
                if (!env_ent_ok(g_env[i])) {
                    g_env[i].p = 0;
                    continue;
                }
                if (!g_sky_take_all
                    && mem::read<uint8_t>(g_env[i].p + schema::C_EnvSky::m_bEnabled) == 0)
                    continue;
                call_update_skybox(g_env[i].p);
            }
            g_env_was_on = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    void run_visual_removals() {
        __try {
            if (!game::world_ready())
                return;
            if (!game::local_alive())
                return;
            const auto pawn = game::local_pawn();
            if (!mem::valid(pawn))
                return;

            if (cfg::visuals::no_flash) {
                mem::write<float>(pawn + schema::C_CSPlayerPawnBase::m_flFlashMaxAlpha, 0.f);
                mem::write<float>(pawn + schema::C_CSPlayerPawnBase::m_flFlashDuration, 0.f);
                mem::write<float>(pawn + schema::C_CSPlayerPawnBase::m_flFlashOverlayAlpha, 0.f);
                mem::write<float>(pawn + schema::C_CSPlayerPawnBase::m_flFlashScreenshotAlpha, 0.f);
                mem::write<float>(pawn + schema::C_CSPlayerPawnBase::m_flFlashBangTime, 0.f);
                mem::write<uint8_t>(pawn + schema::C_CSPlayerPawnBase::m_bFlashBuildUp, 0);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    // Appele une seule fois quand la partie se termine: tous les pointeurs d'entites
    // memorises deviennent invalides, on les oublie sans jamais y ecrire.
    void on_world_lost() {
        g_env_n = 0;
        g_env_scan = 0;
        g_sky_take_all = false;
        g_env_was_on = false;
        g_cam_fog.saved = false;
        g_sky3d_fog.saved = false;
        g_tone.saved = false;
        g_post.saved = false;
        g_arms = 0;
        g_arms_saved = false;
        g_hid_overlay = false;
        g_draw_scope_cross = false;
        tools::atm_ents.store(0, std::memory_order_relaxed);
    }

    void run_anti_movement() {}
}
