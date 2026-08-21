#pragma once
#include <string>
#include <vector>

namespace presets {
    struct Data {
        bool combat_aimbot = false;
        bool combat_rage = false;
        bool combat_silent = false;
        bool combat_silent_360 = false;
        bool combat_spinbot = false;
        float combat_spinbot_speed = 90.f;
        bool combat_bhop = false;
        bool combat_airstrafe = false;
        bool combat_fast_stop = false;
        bool combat_ssg_jump = false;
        bool combat_wall_check = false;
        bool combat_autowall = false;
        bool combat_team_check = true;
        bool combat_aim_body = false;
        int combat_aim_priority = 0;
        bool combat_triggerbot = false;
        float combat_trigger_fov = 6.f;
        bool combat_ragebot = false;
        bool combat_ragebot_autoshoot = true;
        bool combat_ragebot_autostop = true;
        bool combat_ragebot_early_stop = true;
        bool combat_ragebot_air_apex = true;
        float combat_ragebot_hitchance = 72.f;
        float combat_ragebot_hitchance_air = 55.f;
        bool combat_ragebot_multitap = true;
        int combat_ragebot_multitap_count = 2;
        bool combat_ragebot_antiaim = true;
        float combat_fov = 150.f;
        int combat_aim_key = 0;
        int combat_silent_key = 0;

        bool visuals_esp = true;
        bool visuals_show_teammates = false;
        bool visuals_skeleton = true;
        bool visuals_glow = false;
        float visuals_glow_color[4] = { 0.62f, 0.22f, 1.f, 1.f };
        float visuals_glow_color_visible[4] = { 1.f, 1.f, 1.f, 1.f };
        bool visuals_glow_split = true;
        bool visuals_item_glow = true;
        bool visuals_names = true;
        bool visuals_avatars = true;
        bool visuals_hp_text = true;
        bool visuals_weapon = true;
        bool visuals_bomb_timer = true;
        bool visuals_snaplines = true;
        bool visuals_offscreen = true;
        bool visuals_fov_circle = true;
        bool visuals_rainbow = false;
        bool visuals_third_person = false;
        float visuals_third_person_dist = 120.f;
        int visuals_third_person_key = 0;
        bool visuals_player_fov = false;
        float visuals_player_fov_value = 100.f;
        bool visuals_no_scope = true;
        bool visuals_no_flash = true;
        bool visuals_hitmarker = true;
        bool visuals_hitmarker_sound_hit = false;
        bool visuals_hitmarker_sound_kill = false;
        bool visuals_tracers = false;
        bool visuals_tracers_local = true;
        bool visuals_tracers_enemy = true;
        bool visuals_tracers_ally = true;
        float visuals_tracers_time = 1.8f;
        float visuals_tracers_color_local[4] = { 0.35f, 0.86f, 1.f, 1.f };
        float visuals_tracers_color_ally[4] = { 0.31f, 1.f, 0.51f, 1.f };
        float visuals_tracers_color_enemy[4] = { 1.f, 0.27f, 0.27f, 1.f };
        bool visuals_spec_list = true;
        bool visuals_atmosphere = false;
        float visuals_atmosphere_color[4] = { 0.72f, 0.22f, 0.95f, 1.f };
        float visuals_atmosphere_night = 70.f;
        float visuals_atmosphere_intensity = 150.f;
        int visuals_esp_key = 0x2D;
        float visuals_esp_color[4] = { 1.f, 1.f, 1.f, 1.f };
        float visuals_fov_color[4] = { 1.f, 1.f, 1.f, 0.45f };

        bool skins_enabled = false;
        bool skins_knife_enabled = false;
        int skins_knife_index = 4;
        int skins_knife_paint = 0;
        float skins_wear = 0.001f;
        int skins_seed = 1;
        int skins_ui_weapon = 0;
        int skins_paint[16]{};
    };

    void capture(Data& out);
    void apply(const Data& in);

    std::vector<std::string> list();
    bool save(const char* name);
    bool load(const char* name);
    bool remove(const char* name);

    inline char last_msg[96]{};
}
