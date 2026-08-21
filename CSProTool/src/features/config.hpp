#pragma once
#include <Windows.h>
#include <cstdint>

namespace cfg {
    inline bool menu_open = true;
        inline int ui_tab = 0; // 0 Ragebot, 1 Aimbot, 2 ESP, 3 Visuels, 4 Skins, 5 Systeme

    namespace combat {
        // Aimbot = snap crosshair si ennemi DANS le cercle
        inline bool aimbot = false;
        // Rage = snap tout l'ecran
        inline bool rage = false;
        // Silent = balle vers la tete sans bouger la cam
        inline bool silent = false;
        // Silent 360 = aussi hors ecran / derriere
        inline bool silent_360 = false;
        // Yaw reseau pour les autres, camera locale inchangee.
        inline bool spinbot = false;
        // Degres par seconde (pas par tick).
        inline float spinbot_speed = 90.f;
        inline int silent_key = 0;

        inline bool bhop = false;
        inline bool airstrafe = false;
        inline bool fast_stop = false;
        inline bool ssg_jump = false;
        inline bool wall_check = false;
        // Wallbang si l'arme penetre (AWP/SSG/AK...). Ignore le mask spotted.
        inline bool autowall = false;
        // ON = ignore les allies (comp). OFF = tout le monde (deathmatch).
        inline bool team_check = true;
        // Traverse les alliés (collision client). Pas les ennemis.
        inline bool aim_body = false;
        // 0 = plus au centre, 1 = HP le plus bas, 2 = plus proche
        inline int aim_priority = 0;
        inline uint64_t focus_id = 0;
        inline char focus_name[64]{};
        inline bool triggerbot = false;
        inline float trigger_fov = 6.f;

        // Ragebot : silent ecran + hitchance + autoshoot + multitap (+ bhop a part).
        inline bool ragebot = false;
        inline bool ragebot_autoshoot = true;
        inline bool ragebot_autostop = true;
        inline bool ragebot_early_stop = false;  // off : moins de refus de tir
        inline bool ragebot_air_apex = true;     // en bhop : privilegie l'apex
        inline float ragebot_hitchance = 55.f;   // % sol (bas = tire plus)
        inline float ragebot_hitchance_air = 35.f;  // % air / apex (SSG laser)
        inline bool ragebot_multitap = false;
        inline int ragebot_multitap_count = 2;
        inline bool ragebot_antiaim = false; // skip AA pour l'instant

        inline float fov = 150.f;
        inline int aim_key = 0; // 0 = toujours actif si le toggle est ON
    }

    namespace visuals {
        inline bool esp = true;
        inline bool show_teammates = false;
        inline bool skeleton = true;
        inline bool glow = false;
        // Violet = cache derriere mur ; blanc = parties visibles (peek).
        inline float glow_color[4] = { 0.62f, 0.22f, 1.f, 1.f };
        inline float glow_color_visible[4] = { 1.f, 1.f, 1.f, 1.f };
        inline bool glow_split = true;
        // Armes / items au sol : glow blanc sur le contour du skin.
        inline bool item_glow = true;
        inline bool names = true;
        inline bool avatars = true;
        inline bool hp_text = true;
        inline bool weapon = true;
        inline bool bomb_timer = true;
        inline bool snaplines = true;
        inline bool offscreen = true;
        inline bool fov_circle = true;
        inline bool rainbow = false;

        inline bool third_person = false;
        inline float third_person_dist = 120.f;
        inline int third_person_key = 0;

        inline bool player_fov = false;
        inline float player_fov_value = 100.f;

        // Overlay noir AWP/SSG off, zoom + croix fine gardes.
        inline bool no_scope = true;
        inline bool no_flash = true;
        inline bool hitmarker = true;
        inline bool hitmarker_sound_hit = false;
        inline bool hitmarker_sound_kill = false;

        inline bool tracers = false;
        inline bool tracers_local = true;
        inline bool tracers_enemy = true;
        inline bool tracers_ally = true;
        inline float tracers_time = 1.8f;
        inline float tracers_color_local[4] = { 0.35f, 0.86f, 1.f, 1.f };
        inline float tracers_color_ally[4] = { 0.31f, 1.f, 0.51f, 1.f };
        inline float tracers_color_enemy[4] = { 1.f, 0.27f, 0.27f, 1.f };

        inline bool spec_list = true;

        inline bool atmosphere = false;
        inline float atmosphere_color[4] = { 0.72f, 0.22f, 0.95f, 1.f };
        inline float atmosphere_night = 70.f;
        inline float atmosphere_intensity = 150.f;

        inline int esp_key = VK_INSERT;
        inline float esp_color[4] = { 1.f, 1.f, 1.f, 1.f };
        inline float fov_color[4] = { 1.f, 1.f, 1.f, 0.45f };
    }

    namespace skins {
        inline bool enabled = false;
        inline bool knife_enabled = false;
        inline int knife_index = 4; // Karambit dans knives::catalog
        inline int knife_paint = 0;
        inline float wear = 0.001f;
        inline int seed = 1;
        inline int ui_weapon = 0;
        inline int paint[16]{};
    }
}
