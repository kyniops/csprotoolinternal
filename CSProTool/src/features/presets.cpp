#include "features/presets.hpp"
#include "features/config.hpp"
#include "features/skins.hpp"
#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace presets {
    static std::string trim(std::string s) {
        auto not_space = [](unsigned char c) { return !std::isspace(c); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
        return s;
    }

    static std::string presets_dir() {
        char app[MAX_PATH]{};
        if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, app)))
            return "presets";
        const std::string root = std::string(app) + "\\CSProTool";
        CreateDirectoryA(root.c_str(), nullptr);
        const std::string dir = root + "\\presets";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir;
    }

    static std::string safe_file_stem(const char* name) {
        std::string out;
        for (const char* p = name; *p; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (std::isalnum(c) || c == '_' || c == '-')
                out += static_cast<char>(c);
            else if (c == ' ')
                out += '_';
        }
        if (out.empty()) out = "preset";
        return out;
    }

    static std::string preset_path(const char* name) {
        return presets_dir() + "\\" + safe_file_stem(name) + ".cfg";
    }

    static void write_f4(std::ostream& o, const char* key, const float* v) {
        o << key << '=' << v[0] << ',' << v[1] << ',' << v[2] << ',' << v[3] << '\n';
    }

    static bool read_f4(const std::string& val, float* v) {
        return sscanf_s(val.c_str(), "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) == 4;
    }

    void capture(Data& out) {
        out.combat_aimbot = cfg::combat::aimbot;
        out.combat_rage = cfg::combat::rage;
        out.combat_silent = cfg::combat::silent;
        out.combat_silent_360 = cfg::combat::silent_360;
        out.combat_spinbot = cfg::combat::spinbot;
        out.combat_spinbot_speed = cfg::combat::spinbot_speed;
        out.combat_bhop = cfg::combat::bhop;
        out.combat_airstrafe = cfg::combat::airstrafe;
        out.combat_fast_stop = cfg::combat::fast_stop;
        out.combat_ssg_jump = cfg::combat::ssg_jump;
        out.combat_wall_check = cfg::combat::wall_check;
        out.combat_autowall = cfg::combat::autowall;
        out.combat_team_check = cfg::combat::team_check;
        out.combat_aim_body = cfg::combat::aim_body;
        out.combat_aim_priority = cfg::combat::aim_priority;
        out.combat_triggerbot = cfg::combat::triggerbot;
        out.combat_trigger_fov = cfg::combat::trigger_fov;
        out.combat_ragebot = cfg::combat::ragebot;
        out.combat_ragebot_autoshoot = cfg::combat::ragebot_autoshoot;
        out.combat_ragebot_autostop = cfg::combat::ragebot_autostop;
        out.combat_ragebot_early_stop = cfg::combat::ragebot_early_stop;
        out.combat_ragebot_air_apex = cfg::combat::ragebot_air_apex;
        out.combat_ragebot_hitchance = cfg::combat::ragebot_hitchance;
        out.combat_ragebot_hitchance_air = cfg::combat::ragebot_hitchance_air;
        out.combat_ragebot_multitap = cfg::combat::ragebot_multitap;
        out.combat_ragebot_multitap_count = cfg::combat::ragebot_multitap_count;
        out.combat_ragebot_antiaim = cfg::combat::ragebot_antiaim;
        out.combat_fov = cfg::combat::fov;
        out.combat_aim_key = cfg::combat::aim_key;
        out.combat_silent_key = cfg::combat::silent_key;

        out.visuals_esp = cfg::visuals::esp;
        out.visuals_show_teammates = cfg::visuals::show_teammates;
        out.visuals_skeleton = cfg::visuals::skeleton;
        out.visuals_glow = cfg::visuals::glow;
        memcpy(out.visuals_glow_color, cfg::visuals::glow_color, sizeof(out.visuals_glow_color));
        memcpy(out.visuals_glow_color_visible, cfg::visuals::glow_color_visible, sizeof(out.visuals_glow_color_visible));
        out.visuals_glow_split = cfg::visuals::glow_split;
        out.visuals_item_glow = cfg::visuals::item_glow;
        out.visuals_names = cfg::visuals::names;
        out.visuals_avatars = cfg::visuals::avatars;
        out.visuals_hp_text = cfg::visuals::hp_text;
        out.visuals_weapon = cfg::visuals::weapon;
        out.visuals_bomb_timer = cfg::visuals::bomb_timer;
        out.visuals_snaplines = cfg::visuals::snaplines;
        out.visuals_offscreen = cfg::visuals::offscreen;
        out.visuals_fov_circle = cfg::visuals::fov_circle;
        out.visuals_rainbow = cfg::visuals::rainbow;
        out.visuals_third_person = cfg::visuals::third_person;
        out.visuals_third_person_dist = cfg::visuals::third_person_dist;
        out.visuals_third_person_key = cfg::visuals::third_person_key;
        out.visuals_player_fov = cfg::visuals::player_fov;
        out.visuals_player_fov_value = cfg::visuals::player_fov_value;
        out.visuals_no_scope = cfg::visuals::no_scope;
        out.visuals_no_flash = cfg::visuals::no_flash;
        out.visuals_hitmarker = cfg::visuals::hitmarker;
        out.visuals_hitmarker_sound_hit = cfg::visuals::hitmarker_sound_hit;
        out.visuals_hitmarker_sound_kill = cfg::visuals::hitmarker_sound_kill;
        out.visuals_tracers = cfg::visuals::tracers;
        out.visuals_tracers_local = cfg::visuals::tracers_local;
        out.visuals_tracers_enemy = cfg::visuals::tracers_enemy;
        out.visuals_tracers_ally = cfg::visuals::tracers_ally;
        out.visuals_tracers_time = cfg::visuals::tracers_time;
        memcpy(out.visuals_tracers_color_local, cfg::visuals::tracers_color_local, sizeof(out.visuals_tracers_color_local));
        memcpy(out.visuals_tracers_color_ally, cfg::visuals::tracers_color_ally, sizeof(out.visuals_tracers_color_ally));
        memcpy(out.visuals_tracers_color_enemy, cfg::visuals::tracers_color_enemy, sizeof(out.visuals_tracers_color_enemy));
        out.visuals_spec_list = cfg::visuals::spec_list;
        out.visuals_atmosphere = cfg::visuals::atmosphere;
        memcpy(out.visuals_atmosphere_color, cfg::visuals::atmosphere_color, sizeof(out.visuals_atmosphere_color));
        out.visuals_atmosphere_night = cfg::visuals::atmosphere_night;
        out.visuals_atmosphere_intensity = cfg::visuals::atmosphere_intensity;
        out.visuals_esp_key = cfg::visuals::esp_key;
        memcpy(out.visuals_esp_color, cfg::visuals::esp_color, sizeof(out.visuals_esp_color));
        memcpy(out.visuals_fov_color, cfg::visuals::fov_color, sizeof(out.visuals_fov_color));

        out.skins_enabled = cfg::skins::enabled;
        out.skins_knife_enabled = cfg::skins::knife_enabled;
        out.skins_knife_index = cfg::skins::knife_index;
        out.skins_knife_paint = cfg::skins::knife_paint;
        out.skins_wear = cfg::skins::wear;
        out.skins_seed = cfg::skins::seed;
        out.skins_ui_weapon = cfg::skins::ui_weapon;
        memcpy(out.skins_paint, cfg::skins::paint, sizeof(out.skins_paint));
    }

    void apply(const Data& in) {
        cfg::combat::aimbot = in.combat_aimbot;
        cfg::combat::rage = in.combat_rage;
        cfg::combat::silent = in.combat_silent;
        cfg::combat::silent_360 = in.combat_silent_360;
        cfg::combat::spinbot = in.combat_spinbot;
        cfg::combat::spinbot_speed = in.combat_spinbot_speed;
        cfg::combat::bhop = in.combat_bhop;
        cfg::combat::airstrafe = in.combat_airstrafe;
        cfg::combat::fast_stop = in.combat_fast_stop;
        cfg::combat::ssg_jump = in.combat_ssg_jump;
        cfg::combat::wall_check = in.combat_wall_check;
        cfg::combat::autowall = in.combat_autowall;
        cfg::combat::team_check = in.combat_team_check;
        cfg::combat::aim_body = in.combat_aim_body;
        cfg::combat::aim_priority = in.combat_aim_priority;
        cfg::combat::triggerbot = in.combat_triggerbot;
        cfg::combat::trigger_fov = in.combat_trigger_fov;
        cfg::combat::ragebot = in.combat_ragebot;
        cfg::combat::ragebot_autoshoot = in.combat_ragebot_autoshoot;
        cfg::combat::ragebot_autostop = in.combat_ragebot_autostop;
        cfg::combat::ragebot_early_stop = in.combat_ragebot_early_stop;
        cfg::combat::ragebot_air_apex = in.combat_ragebot_air_apex;
        cfg::combat::ragebot_hitchance = in.combat_ragebot_hitchance;
        cfg::combat::ragebot_hitchance_air = in.combat_ragebot_hitchance_air;
        cfg::combat::ragebot_multitap = in.combat_ragebot_multitap;
        cfg::combat::ragebot_multitap_count = in.combat_ragebot_multitap_count;
        cfg::combat::ragebot_antiaim = in.combat_ragebot_antiaim;
        cfg::combat::fov = in.combat_fov;
        cfg::combat::aim_key = in.combat_aim_key;
        cfg::combat::silent_key = in.combat_silent_key;

        cfg::visuals::esp = in.visuals_esp;
        cfg::visuals::show_teammates = in.visuals_show_teammates;
        cfg::visuals::skeleton = in.visuals_skeleton;
        cfg::visuals::glow = in.visuals_glow;
        memcpy(cfg::visuals::glow_color, in.visuals_glow_color, sizeof(cfg::visuals::glow_color));
        memcpy(cfg::visuals::glow_color_visible, in.visuals_glow_color_visible, sizeof(cfg::visuals::glow_color_visible));
        cfg::visuals::glow_split = in.visuals_glow_split;
        cfg::visuals::item_glow = in.visuals_item_glow;
        cfg::visuals::names = in.visuals_names;
        cfg::visuals::avatars = in.visuals_avatars;
        cfg::visuals::hp_text = in.visuals_hp_text;
        cfg::visuals::weapon = in.visuals_weapon;
        cfg::visuals::bomb_timer = in.visuals_bomb_timer;
        cfg::visuals::snaplines = in.visuals_snaplines;
        cfg::visuals::offscreen = in.visuals_offscreen;
        cfg::visuals::fov_circle = in.visuals_fov_circle;
        cfg::visuals::rainbow = in.visuals_rainbow;
        cfg::visuals::third_person = in.visuals_third_person;
        cfg::visuals::third_person_dist = in.visuals_third_person_dist;
        cfg::visuals::third_person_key = in.visuals_third_person_key;
        cfg::visuals::player_fov = in.visuals_player_fov;
        cfg::visuals::player_fov_value = in.visuals_player_fov_value;
        cfg::visuals::no_scope = in.visuals_no_scope;
        cfg::visuals::no_flash = in.visuals_no_flash;
        cfg::visuals::hitmarker = in.visuals_hitmarker;
        cfg::visuals::hitmarker_sound_hit = in.visuals_hitmarker_sound_hit;
        cfg::visuals::hitmarker_sound_kill = in.visuals_hitmarker_sound_kill;
        cfg::visuals::tracers = in.visuals_tracers;
        cfg::visuals::tracers_local = in.visuals_tracers_local;
        cfg::visuals::tracers_enemy = in.visuals_tracers_enemy;
        cfg::visuals::tracers_ally = in.visuals_tracers_ally;
        cfg::visuals::tracers_time = in.visuals_tracers_time;
        memcpy(cfg::visuals::tracers_color_local, in.visuals_tracers_color_local, sizeof(cfg::visuals::tracers_color_local));
        memcpy(cfg::visuals::tracers_color_ally, in.visuals_tracers_color_ally, sizeof(cfg::visuals::tracers_color_ally));
        memcpy(cfg::visuals::tracers_color_enemy, in.visuals_tracers_color_enemy, sizeof(cfg::visuals::tracers_color_enemy));
        cfg::visuals::spec_list = in.visuals_spec_list;
        cfg::visuals::atmosphere = in.visuals_atmosphere;
        memcpy(cfg::visuals::atmosphere_color, in.visuals_atmosphere_color, sizeof(cfg::visuals::atmosphere_color));
        cfg::visuals::atmosphere_night = in.visuals_atmosphere_night;
        cfg::visuals::atmosphere_intensity = in.visuals_atmosphere_intensity;
        cfg::visuals::esp_key = in.visuals_esp_key;
        memcpy(cfg::visuals::esp_color, in.visuals_esp_color, sizeof(cfg::visuals::esp_color));
        memcpy(cfg::visuals::fov_color, in.visuals_fov_color, sizeof(cfg::visuals::fov_color));

        cfg::skins::enabled = in.skins_enabled;
        cfg::skins::knife_enabled = in.skins_knife_enabled;
        cfg::skins::knife_index = in.skins_knife_index;
        cfg::skins::knife_paint = in.skins_knife_paint;
        cfg::skins::wear = in.skins_wear;
        cfg::skins::seed = in.skins_seed;
        cfg::skins::ui_weapon = in.skins_ui_weapon;
        memcpy(cfg::skins::paint, in.skins_paint, sizeof(cfg::skins::paint));
        skins::force_refresh();
    }

    static bool write_file(const char* display_name, const Data& d) {
        std::ofstream f(preset_path(display_name), std::ios::trunc);
        if (!f) return false;
        f << "version=2\n";
        f << "name=" << display_name << '\n';
        f << "combat.aimbot=" << d.combat_aimbot << '\n';
        f << "combat.rage=" << d.combat_rage << '\n';
        f << "combat.silent=" << d.combat_silent << '\n';
        f << "combat.silent_360=" << d.combat_silent_360 << '\n';
        f << "combat.spinbot=" << d.combat_spinbot << '\n';
        f << "combat.spinbot_speed=" << d.combat_spinbot_speed << '\n';
        f << "combat.bhop=" << d.combat_bhop << '\n';
        f << "combat.airstrafe=" << d.combat_airstrafe << '\n';
        f << "combat.fast_stop=" << d.combat_fast_stop << '\n';
        f << "combat.ssg_jump=" << d.combat_ssg_jump << '\n';
        f << "combat.wall_check=" << d.combat_wall_check << '\n';
        f << "combat.autowall=" << d.combat_autowall << '\n';
        f << "combat.team_check=" << d.combat_team_check << '\n';
        f << "combat.aim_body=" << d.combat_aim_body << '\n';
        f << "combat.aim_priority=" << d.combat_aim_priority << '\n';
        f << "combat.triggerbot=" << d.combat_triggerbot << '\n';
        f << "combat.trigger_fov=" << d.combat_trigger_fov << '\n';
        f << "combat.ragebot=" << d.combat_ragebot << '\n';
        f << "combat.ragebot_autoshoot=" << d.combat_ragebot_autoshoot << '\n';
        f << "combat.ragebot_autostop=" << d.combat_ragebot_autostop << '\n';
        f << "combat.ragebot_early_stop=" << d.combat_ragebot_early_stop << '\n';
        f << "combat.ragebot_air_apex=" << d.combat_ragebot_air_apex << '\n';
        f << "combat.ragebot_hitchance=" << d.combat_ragebot_hitchance << '\n';
        f << "combat.ragebot_hitchance_air=" << d.combat_ragebot_hitchance_air << '\n';
        f << "combat.ragebot_multitap=" << d.combat_ragebot_multitap << '\n';
        f << "combat.ragebot_multitap_count=" << d.combat_ragebot_multitap_count << '\n';
        f << "combat.ragebot_antiaim=" << d.combat_ragebot_antiaim << '\n';
        f << "combat.fov=" << d.combat_fov << '\n';
        f << "combat.aim_key=" << d.combat_aim_key << '\n';
        f << "combat.silent_key=" << d.combat_silent_key << '\n';
        f << "visuals.esp=" << d.visuals_esp << '\n';
        f << "visuals.show_teammates=" << d.visuals_show_teammates << '\n';
        f << "visuals.skeleton=" << d.visuals_skeleton << '\n';
        f << "visuals.glow=" << d.visuals_glow << '\n';
        write_f4(f, "visuals.glow_color", d.visuals_glow_color);
        write_f4(f, "visuals.glow_color_visible", d.visuals_glow_color_visible);
        f << "visuals.glow_split=" << d.visuals_glow_split << '\n';
        f << "visuals.item_glow=" << d.visuals_item_glow << '\n';
        f << "visuals.names=" << d.visuals_names << '\n';
        f << "visuals.avatars=" << d.visuals_avatars << '\n';
        f << "visuals.hp_text=" << d.visuals_hp_text << '\n';
        f << "visuals.weapon=" << d.visuals_weapon << '\n';
        f << "visuals.bomb_timer=" << d.visuals_bomb_timer << '\n';
        f << "visuals.snaplines=" << d.visuals_snaplines << '\n';
        f << "visuals.offscreen=" << d.visuals_offscreen << '\n';
        f << "visuals.fov_circle=" << d.visuals_fov_circle << '\n';
        f << "visuals.rainbow=" << d.visuals_rainbow << '\n';
        f << "visuals.third_person=" << d.visuals_third_person << '\n';
        f << "visuals.third_person_dist=" << d.visuals_third_person_dist << '\n';
        f << "visuals.third_person_key=" << d.visuals_third_person_key << '\n';
        f << "visuals.player_fov=" << d.visuals_player_fov << '\n';
        f << "visuals.player_fov_value=" << d.visuals_player_fov_value << '\n';
        f << "visuals.no_scope=" << d.visuals_no_scope << '\n';
        f << "visuals.no_flash=" << d.visuals_no_flash << '\n';
        f << "visuals.hitmarker=" << d.visuals_hitmarker << '\n';
        f << "visuals.hitmarker_sound_hit=" << d.visuals_hitmarker_sound_hit << '\n';
        f << "visuals.hitmarker_sound_kill=" << d.visuals_hitmarker_sound_kill << '\n';
        f << "visuals.tracers=" << d.visuals_tracers << '\n';
        f << "visuals.tracers_local=" << d.visuals_tracers_local << '\n';
        f << "visuals.tracers_enemy=" << d.visuals_tracers_enemy << '\n';
        f << "visuals.tracers_ally=" << d.visuals_tracers_ally << '\n';
        f << "visuals.tracers_time=" << d.visuals_tracers_time << '\n';
        write_f4(f, "visuals.tracers_color_local", d.visuals_tracers_color_local);
        write_f4(f, "visuals.tracers_color_ally", d.visuals_tracers_color_ally);
        write_f4(f, "visuals.tracers_color_enemy", d.visuals_tracers_color_enemy);
        f << "visuals.spec_list=" << d.visuals_spec_list << '\n';
        f << "visuals.atmosphere=" << d.visuals_atmosphere << '\n';
        write_f4(f, "visuals.atmosphere_color", d.visuals_atmosphere_color);
        f << "visuals.atmosphere_night=" << d.visuals_atmosphere_night << '\n';
        f << "visuals.atmosphere_intensity=" << d.visuals_atmosphere_intensity << '\n';
        f << "visuals.esp_key=" << d.visuals_esp_key << '\n';
        write_f4(f, "visuals.esp_color", d.visuals_esp_color);
        write_f4(f, "visuals.fov_color", d.visuals_fov_color);
        f << "skins.enabled=" << d.skins_enabled << '\n';
        f << "skins.knife_enabled=" << d.skins_knife_enabled << '\n';
        f << "skins.knife_index=" << d.skins_knife_index << '\n';
        f << "skins.knife_paint=" << d.skins_knife_paint << '\n';
        f << "skins.wear=" << d.skins_wear << '\n';
        f << "skins.seed=" << d.skins_seed << '\n';
        f << "skins.ui_weapon=" << d.skins_ui_weapon << '\n';
        f << "skins.paint=";
        for (int i = 0; i < 16; ++i) {
            if (i) f << ',';
            f << d.skins_paint[i];
        }
        f << '\n';
        return f.good();
    }

    static bool parse_file(const std::string& path, Data& d, std::string& display_name) {
        std::ifstream f(path);
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = trim(line.substr(0, eq));
            const std::string val = trim(line.substr(eq + 1));
            if (key == "name") { display_name = val; continue; }
            auto b = [&](bool& x) { x = (val == "1" || val == "true"); };
            auto i = [&](int& x) { x = std::atoi(val.c_str()); };
            auto fl = [&](float& x) { x = static_cast<float>(std::atof(val.c_str())); };
            if (key == "combat.aimbot") b(d.combat_aimbot);
            else if (key == "combat.rage") b(d.combat_rage);
            else if (key == "combat.silent") b(d.combat_silent);
            else if (key == "combat.silent_360") b(d.combat_silent_360);
            else if (key == "combat.spinbot") b(d.combat_spinbot);
            else if (key == "combat.spinbot_speed") fl(d.combat_spinbot_speed);
            else if (key == "combat.bhop") b(d.combat_bhop);
            else if (key == "combat.airstrafe") b(d.combat_airstrafe);
            else if (key == "combat.fast_stop") b(d.combat_fast_stop);
            else if (key == "combat.ssg_jump") b(d.combat_ssg_jump);
            else if (key == "combat.wall_check") b(d.combat_wall_check);
            else if (key == "combat.autowall") b(d.combat_autowall);
            else if (key == "combat.team_check") b(d.combat_team_check);
            else if (key == "combat.aim_body") b(d.combat_aim_body);
            else if (key == "combat.aim_priority") i(d.combat_aim_priority);
            else if (key == "combat.triggerbot") b(d.combat_triggerbot);
            else if (key == "combat.trigger_fov") fl(d.combat_trigger_fov);
            else if (key == "combat.ragebot") b(d.combat_ragebot);
            else if (key == "combat.ragebot_autoshoot") b(d.combat_ragebot_autoshoot);
            else if (key == "combat.ragebot_autostop") b(d.combat_ragebot_autostop);
            else if (key == "combat.ragebot_early_stop") b(d.combat_ragebot_early_stop);
            else if (key == "combat.ragebot_air_apex") b(d.combat_ragebot_air_apex);
            else if (key == "combat.ragebot_hitchance") fl(d.combat_ragebot_hitchance);
            else if (key == "combat.ragebot_hitchance_air") fl(d.combat_ragebot_hitchance_air);
            else if (key == "combat.ragebot_multitap") b(d.combat_ragebot_multitap);
            else if (key == "combat.ragebot_multitap_count") i(d.combat_ragebot_multitap_count);
            else if (key == "combat.ragebot_antiaim") b(d.combat_ragebot_antiaim);
            else if (key == "combat.fov") fl(d.combat_fov);
            else if (key == "combat.aim_key") i(d.combat_aim_key);
            else if (key == "combat.silent_key") i(d.combat_silent_key);
            else if (key == "visuals.esp") b(d.visuals_esp);
            else if (key == "visuals.show_teammates") b(d.visuals_show_teammates);
            else if (key == "visuals.skeleton") b(d.visuals_skeleton);
            else if (key == "visuals.glow") b(d.visuals_glow);
            else if (key == "visuals.glow_color") read_f4(val, d.visuals_glow_color);
            else if (key == "visuals.glow_color_visible") read_f4(val, d.visuals_glow_color_visible);
            else if (key == "visuals.glow_split") b(d.visuals_glow_split);
            else if (key == "visuals.item_glow") b(d.visuals_item_glow);
            else if (key == "visuals.names") b(d.visuals_names);
            else if (key == "visuals.avatars") b(d.visuals_avatars);
            else if (key == "visuals.hp_text") b(d.visuals_hp_text);
            else if (key == "visuals.weapon") b(d.visuals_weapon);
            else if (key == "visuals.bomb_timer") b(d.visuals_bomb_timer);
            else if (key == "visuals.snaplines") b(d.visuals_snaplines);
            else if (key == "visuals.offscreen") b(d.visuals_offscreen);
            else if (key == "visuals.fov_circle") b(d.visuals_fov_circle);
            else if (key == "visuals.rainbow") b(d.visuals_rainbow);
            else if (key == "visuals.third_person") b(d.visuals_third_person);
            else if (key == "visuals.third_person_dist") fl(d.visuals_third_person_dist);
            else if (key == "visuals.third_person_key") i(d.visuals_third_person_key);
            else if (key == "visuals.player_fov") b(d.visuals_player_fov);
            else if (key == "visuals.player_fov_value") fl(d.visuals_player_fov_value);
            else if (key == "visuals.no_scope") b(d.visuals_no_scope);
            else if (key == "visuals.no_flash") b(d.visuals_no_flash);
            else if (key == "visuals.hitmarker") b(d.visuals_hitmarker);
            else if (key == "visuals.hitmarker_sound_hit") b(d.visuals_hitmarker_sound_hit);
            else if (key == "visuals.hitmarker_sound_kill") b(d.visuals_hitmarker_sound_kill);
            else if (key == "visuals.tracers") b(d.visuals_tracers);
            else if (key == "visuals.tracers_local") b(d.visuals_tracers_local);
            else if (key == "visuals.tracers_enemy") b(d.visuals_tracers_enemy);
            else if (key == "visuals.tracers_ally") b(d.visuals_tracers_ally);
            else if (key == "visuals.tracers_time") fl(d.visuals_tracers_time);
            else if (key == "visuals.tracers_color_local") read_f4(val, d.visuals_tracers_color_local);
            else if (key == "visuals.tracers_color_ally") read_f4(val, d.visuals_tracers_color_ally);
            else if (key == "visuals.tracers_color_enemy") read_f4(val, d.visuals_tracers_color_enemy);
            else if (key == "visuals.spec_list") b(d.visuals_spec_list);
            else if (key == "visuals.atmosphere") b(d.visuals_atmosphere);
            else if (key == "visuals.atmosphere_color") read_f4(val, d.visuals_atmosphere_color);
            else if (key == "visuals.atmosphere_night") fl(d.visuals_atmosphere_night);
            else if (key == "visuals.atmosphere_intensity") fl(d.visuals_atmosphere_intensity);
            else if (key == "visuals.esp_key") i(d.visuals_esp_key);
            else if (key == "visuals.esp_color") read_f4(val, d.visuals_esp_color);
            else if (key == "visuals.fov_color") read_f4(val, d.visuals_fov_color);
            else if (key == "skins.enabled") b(d.skins_enabled);
            else if (key == "skins.knife_enabled") b(d.skins_knife_enabled);
            else if (key == "skins.knife_index") i(d.skins_knife_index);
            else if (key == "skins.knife_paint") i(d.skins_knife_paint);
            else if (key == "skins.wear") fl(d.skins_wear);
            else if (key == "skins.seed") i(d.skins_seed);
            else if (key == "skins.ui_weapon") i(d.skins_ui_weapon);
            else if (key == "skins.paint") {
                int idx = 0;
                size_t start = 0;
                while (start <= val.size() && idx < 16) {
                    const size_t comma = val.find(',', start);
                    const std::string part = trim(val.substr(start, comma == std::string::npos
                        ? std::string::npos : comma - start));
                    if (!part.empty())
                        d.skins_paint[idx++] = std::atoi(part.c_str());
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            }
        }
        return true;
    }

    std::vector<std::string> list() {
        std::vector<std::string> names;
        const std::string dir = presets_dir();
        WIN32_FIND_DATAA fd{};
        const HANDLE h = FindFirstFileA((dir + "\\*.cfg").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return names;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            Data d{};
            std::string display;
            const std::string path = dir + "\\" + fd.cFileName;
            if (parse_file(path, d, display) && !display.empty())
                names.push_back(display);
            else {
                std::string stem = fd.cFileName;
                const auto dot = stem.rfind('.');
                if (dot != std::string::npos) stem = stem.substr(0, dot);
                names.push_back(stem);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        std::sort(names.begin(), names.end());
        return names;
    }

    bool save(const char* name) {
        if (!name || !*name) {
            wsprintfA(last_msg, "Nom vide");
            return false;
        }
        Data d{};
        capture(d);
        if (!write_file(name, d)) {
            wsprintfA(last_msg, "Erreur sauvegarde");
            return false;
        }
        wsprintfA(last_msg, "Preset \"%s\" sauvegarde", name);
        return true;
    }

    bool load(const char* name) {
        if (!name || !*name) return false;
        Data d{};
        std::string display;
        if (!parse_file(preset_path(name), d, display)) {
            wsprintfA(last_msg, "Preset introuvable");
            return false;
        }
        apply(d);
        wsprintfA(last_msg, "Preset \"%s\" applique", display.empty() ? name : display.c_str());
        return true;
    }

    bool remove(const char* name) {
        if (!name || !*name) return false;
        if (!DeleteFileA(preset_path(name).c_str())) {
            wsprintfA(last_msg, "Suppression echouee");
            return false;
        }
        wsprintfA(last_msg, "Preset \"%s\" supprime", name);
        return true;
    }
}
