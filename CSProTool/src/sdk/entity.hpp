#pragma once
#include "common.hpp"
#include "core/memory.hpp"
#include "core/modules.hpp"
#include "sdk/schema.hpp"
#include <cfloat>
#include <cmath>
#include <cstdint>

namespace game {
    inline constexpr uintptr_t k_list_base = 0x10;
    inline constexpr uintptr_t k_ent_stride = 0x70;

    inline constexpr int k_bone_ids[] = {
        1, 2, 6, 7, 8, 9, 10, 11, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23
    };
    inline constexpr int k_bone_links[][2] = {
        {1,2},{2,23},{23,6},{6,7},{7,8},
        {6,9},{9,10},{10,11},
        {6,13},{13,14},{14,15},
        {1,17},{17,18},{18,19},
        {1,20},{20,21},{21,22},
    };

    struct Player {
        uintptr_t controller{};
        uintptr_t pawn{};
        int health{};
        int team{};
        bool teammate{};
        uint64_t steamid{};
        std::string name;
        Vec3 origin{};
        Vec3 head{};   // highest skeleton point (aim / box top)
        Vec3 feet{};
        float bone_lift{}; // Z correction applied to bones
    };

    inline uint64_t player_id(const Player& p) {
        if (p.steamid) return p.steamid;
        return static_cast<uint64_t>(p.controller);
    }

    struct CollectStats {
        int scanned = 0;
        int controllers = 0;
        int pawns = 0;
        int valid = 0;
        uintptr_t list = 0;
    };

    inline uintptr_t client_base() { return modules::client(); }

    inline uintptr_t entity_list() {
        const auto client = client_base();
        if (!client) return 0;
        return mem::read<uintptr_t>(client + schema::off::client_dll::dwEntityList);
    }

    inline uintptr_t get_entity(uint32_t index_or_handle) {
        const auto list = entity_list();
        if (!mem::valid(list)) return 0;
        const uint32_t idx = index_or_handle & 0x7FFFu;
        const auto entry = mem::read<uintptr_t>(list + k_list_base + 8ull * (idx >> 9));
        if (!mem::valid(entry)) return 0;
        return mem::read<uintptr_t>(entry + k_ent_stride * (idx & 0x1FFu));
    }

    inline bool designer_name(uintptr_t ent, char* out, size_t n) {
        if (!mem::valid(ent) || !out || n < 4) return false;
        out[0] = 0;
        const auto ident = mem::read<uintptr_t>(ent + schema::CEntityInstance::m_pEntity);
        if (!mem::valid(ident)) return false;
        for (const auto off : {
            schema::CEntityIdentity::m_designerName,
            schema::CEntityIdentity::m_name }) {
            auto p = mem::read<uintptr_t>(ident + off);
            p &= ~7ull;
            if (!mem::valid(p)) continue;
            char tmp[72]{};
            if (!mem::read_raw(p, tmp, sizeof(tmp) - 1)) continue;
            if (tmp[0] < 'A' || tmp[0] > 'z') continue;
            bool ok = true;
            for (int i = 0; i < 71 && tmp[i]; ++i) {
                if (tmp[i] < 32) { ok = false; break; }
            }
            if (!ok) continue;
            size_t i = 0;
            for (; i + 1 < n && tmp[i]; ++i)
                out[i] = tmp[i];
            out[i] = 0;
            return true;
        }
        return false;
    }

    inline uintptr_t local_pawn() {
        const auto client = client_base();
        if (!client) return 0;
        auto pawn = mem::read<uintptr_t>(client + schema::off::client_dll::dwLocalPlayerPawn);
        if (mem::valid(pawn)) return pawn;
        const auto ctrl = mem::read<uintptr_t>(client + schema::off::client_dll::dwLocalPlayerController);
        if (!mem::valid(ctrl)) return 0;
        const auto handle = mem::read<uint32_t>(ctrl + schema::CCSPlayerController::m_hPlayerPawn);
        return handle ? get_entity(handle) : 0;
    }

    inline ViewMatrix view_matrix() {
        return mem::read<ViewMatrix>(client_base() + schema::off::client_dll::dwViewMatrix);
    }

    inline Vec3 abs_origin(uintptr_t pawn) {
        if (!mem::valid(pawn)) return {};
        const auto node = mem::read<uintptr_t>(pawn + schema::C_BaseEntity::m_pGameSceneNode);
        if (mem::valid(node)) {
            auto o = mem::read<Vec3>(node + schema::CGameSceneNode::m_vecAbsOrigin);
            if (o.length() > 1.f && std::isfinite(o.x)) return o;
        }
        return mem::read<Vec3>(pawn + schema::C_BasePlayerPawn::m_vOldOrigin);
    }

    inline uintptr_t bone_array_ptr(uintptr_t pawn) {
        if (!mem::valid(pawn)) return 0;
        const auto node = mem::read<uintptr_t>(pawn + schema::C_BaseEntity::m_pGameSceneNode);
        if (!mem::valid(node)) return 0;
        return mem::read<uintptr_t>(node + schema::k_bone_array);
    }

    inline Vec3 bone_raw(uintptr_t bones, int bone) {
        if (!mem::valid(bones)) return {};
        return mem::read<Vec3>(bones + static_cast<uintptr_t>(bone) * 32ull);
    }

    inline Vec3 bone_pos(uintptr_t pawn, int bone, float lift_z = 0.f) {
        if (!mem::valid(pawn)) return {};
        auto p = bone_raw(bone_array_ptr(pawn), bone);
        if (!std::isfinite(p.x) || p.length() < 1.f) return {};
        p.z += lift_z;
        return p;
    }

    // Like external AlignSkeletonToAbsOrigin + highest bone
    inline bool resolve_skeleton(uintptr_t pawn, Vec3& head, Vec3& feet, float& lift_z) {
        head = {};
        feet = {};
        lift_z = 0.f;
        const auto bones = bone_array_ptr(pawn);
        if (!mem::valid(bones)) return false;

        const Vec3 origin = abs_origin(pawn);
        float best_z = -FLT_MAX;
        float low_z = FLT_MAX;
        bool any = false;

        for (int id : k_bone_ids) {
            auto b = bone_raw(bones, id);
            if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.z)) continue;
            if (b.length() < 1.f) continue;
            any = true;
            if (b.z > best_z) { best_z = b.z; head = b; }
            if (b.z < low_z) low_z = b.z;
        }
        if (!any) return false;

        // Feet = ankles average
        const auto l = bone_raw(bones, 19);
        const auto r = bone_raw(bones, 22);
        if (l.length() > 1.f && r.length() > 1.f)
            feet = Vec3{ (l.x + r.x) * 0.5f, (l.y + r.y) * 0.5f, (std::min)(l.z, r.z) };
        else if (l.length() > 1.f) feet = l;
        else if (r.length() > 1.f) feet = r;
        else if (origin.length() > 1.f) feet = origin;
        else feet = head;

        // Lift skeleton if bones sit below pawn ground
        if (origin.length() > 1.f && low_z < FLT_MAX * 0.5f) {
            const float lift = origin.z - low_z;
            if (lift > 0.5f && lift < 48.f) {
                lift_z = lift;
                head.z += lift;
                feet.z += lift;
            } else {
                feet.z = origin.z; // pin feet to ground
            }
        }

        // Recompute highest after lift for safety
        if (lift_z != 0.f) {
            best_z = -FLT_MAX;
            for (int id : k_bone_ids) {
                auto b = bone_raw(bones, id);
                if (b.length() < 1.f) continue;
                b.z += lift_z;
                if (b.z > best_z) { best_z = b.z; head = b; }
            }
        }
        return head.length() > 1.f;
    }

    // Head aim: highest of bones 5–8 (+ small Z into hitbox) — matches CS2 hitboxes
    inline Vec3 aim_head(uintptr_t pawn) {
        if (!mem::valid(pawn)) return {};
        Vec3 head{}, feet{};
        float lift = 0.f;
        resolve_skeleton(pawn, head, feet, lift);

        const auto bones = bone_array_ptr(pawn);
        Vec3 best{};
        float best_z = -FLT_MAX;
        for (int id : { 5, 6, 7, 8 }) {
            auto b = bone_raw(bones, id);
            if (b.length() < 1.f) continue;
            b.z += lift;
            if (b.z > best_z) {
                best_z = b.z;
                best = b;
            }
        }

        // Petit lift dans la hitbox tete (pas trop haut — rate le haut du crane)
        constexpr float k_head_lift = 2.5f;
        if (best.length() > 1.f) {
            best.z += k_head_lift;
            return best;
        }
        if (head.length() > 1.f) {
            head.z += k_head_lift;
            return head;
        }
        const auto o = abs_origin(pawn);
        return o.length() > 1.f ? o + Vec3{ 0, 0, 72 } : Vec3{};
    }

    inline Vec3 aim_pos(uintptr_t pawn, bool body) {
        if (!mem::valid(pawn)) return {};
        if (body) {
            Vec3 head{}, feet{};
            float lift = 0.f;
            resolve_skeleton(pawn, head, feet, lift);
            const Vec3 chest = bone_pos(pawn, 23, lift);
            const Vec3 pelvis = bone_pos(pawn, 2, lift);
            if (chest.length() > 1.f && pelvis.length() > 1.f) {
                return {
                    chest.x * 0.62f + pelvis.x * 0.38f,
                    chest.y * 0.62f + pelvis.y * 0.38f,
                    chest.z * 0.55f + pelvis.z * 0.45f
                };
            }
            if (chest.length() > 1.f)
                return chest;
            if (pelvis.length() > 1.f)
                return pelvis + Vec3{ 0.f, 0.f, 12.f };
            if (head.length() > 1.f && feet.length() > 1.f)
                return feet + (head - feet) * 0.52f;
            const auto o = abs_origin(pawn);
            return o.length() > 1.f ? o + Vec3{ 0.f, 0.f, 42.f } : Vec3{};
        }
        return aim_head(pawn);
    }

    inline uintptr_t active_weapon(uintptr_t pawn) {
        if (!mem::valid(pawn)) return 0;
        const auto ws = mem::read<uintptr_t>(pawn + schema::C_BasePlayerPawn::m_pWeaponServices);
        if (!mem::valid(ws)) return 0;
        const auto handle = mem::read<uint32_t>(ws + schema::CPlayer_WeaponServices::m_hActiveWeapon);
        if (!handle || handle == 0xFFFFFFFFu) return 0;
        return get_entity(handle);
    }

    // Precision legere pour SSG jump — champs minimaux, jamais de wipe punch.
    inline void force_no_spread(uintptr_t pawn) {
        if (!mem::valid(pawn)) return;
        mem::write<float>(pawn + schema::C_CSPlayerPawn::m_flVelocityModifier, 1.f);
        const auto wpn = active_weapon(pawn);
        if (!mem::valid(wpn)) return;
        mem::write<float>(wpn + schema::C_CSWeaponBase::m_fAccuracyPenalty, 0.f);
        mem::write<float>(wpn + schema::C_CSWeaponBase::m_flTurningInaccuracy, 0.f);
    }

    inline int local_player_index() {
        const auto ctrl = mem::read<uintptr_t>(
            client_base() + schema::off::client_dll::dwLocalPlayerController);
        if (!mem::valid(ctrl)) return 0;
        for (int i = 1; i <= 64; ++i) {
            if (get_entity(static_cast<uint32_t>(i)) == ctrl)
                return i;
        }
        return 0;
    }

    // Visible UNIQUEMENT si TOI tu le vois (mask local).
    // m_bSpotted global reste true a travers les murs -> on ne l'utilise pas.
    inline bool is_visible(uintptr_t pawn) {
        if (!mem::valid(pawn)) return false;
        const auto base = pawn + schema::C_CSPlayerPawn::m_entitySpottedState;
        const auto mask0 = mem::read<uint32_t>(base + schema::EntitySpottedState_t::m_bSpottedByMask);
        const auto mask1 = mem::read<uint32_t>(base + schema::EntitySpottedState_t::m_bSpottedByMask + 4);

        const int idx = local_player_index();
        if (idx <= 0) return false;
        const uint32_t bit = static_cast<uint32_t>(idx - 1);
        if (bit < 32) return (mask0 & (1u << bit)) != 0;
        if (bit < 64) return (mask1 & (1u << (bit - 32))) != 0;
        return false;
    }

    inline Vec3 eye_pos(uintptr_t pawn) {
        const auto o = abs_origin(pawn);
        const auto vo = mem::read<Vec3>(pawn + schema::C_BaseModelEntity::m_vecViewOffset);
        if (vo.length() > 1.f) return o + vo;
        return o + Vec3{ 0, 0, 64 };
    }

    inline bool can_wallbang(uintptr_t target) {
        if (!cfg::combat::autowall)
            return false;
        const auto local = local_pawn();
        const auto wpn = active_weapon(local);
        if (!mem::valid(local) || !mem::valid(wpn) || !mem::valid(target))
            return false;
        const auto vdata = mem::read<uintptr_t>(wpn + schema::C_BaseEntity::m_nSubclassID + 0x8);
        if (!mem::valid(vdata))
            return false;
        const float pen = mem::read<float>(vdata + schema::CCSWeaponBaseVData::m_flPenetration);
        if (!std::isfinite(pen) || pen < 1.9f)
            return false;
        float range = mem::read<float>(vdata + schema::CCSWeaponBaseVData::m_flRange);
        if (!std::isfinite(range) || range < 400.f)
            range = 4096.f;
        const Vec3 from = eye_pos(local);
        Vec3 to = aim_head(target);
        if (to.length() < 1.f)
            to = abs_origin(target) + Vec3{ 0.f, 0.f, 64.f };
        const float dist = (to - from).length();
        const float maxd = (pen >= 2.4f) ? range * 0.55f : range * 0.32f;
        return dist <= maxd;
    }

    inline bool wall_ok(uintptr_t pawn, bool ignore_walls = false) {
        if (ignore_walls || !cfg::combat::wall_check)
            return true;
        if (is_visible(pawn))
            return true;
        return can_wallbang(pawn);
    }

    // dwCSGOInput : objet global (vtable en premier qword) OU pointeur vers l'objet.
    inline uintptr_t csgo_input() {
        const auto client = client_base();
        if (!client) return 0;
        const auto slot = client + schema::off::client_dll::dwCSGOInput;
        const auto first = mem::read<uintptr_t>(slot);
        const auto in_client = [&](uintptr_t p) {
            return p > client && p < client + 0x08000000ull;
        };
        if (in_client(first))
            return slot;
        if (mem::valid(first) && in_client(mem::read<uintptr_t>(first)))
            return first;
        return slot;
    }

    inline uintptr_t view_angles_addr() {
        const auto input = csgo_input();
        if (input)
            return input + schema::k_input_angles;
        return client_base() + schema::off::client_dll::dwViewAngles;
    }

    inline Vec3 view_angles() { return mem::read<Vec3>(view_angles_addr()); }

    inline void wish_from_velocity(uintptr_t pawn, float& fwd, float& left) {
        fwd = 0.f;
        left = 0.f;
        if (!mem::valid(pawn)) return;
        const Vec3 vel = mem::read<Vec3>(pawn + schema::C_BaseEntity::m_vecAbsVelocity);
        if (!std::isfinite(vel.x) || !std::isfinite(vel.y))
            return;
        if (vel.length2d() < 12.f)
            return;
        const Vec3 view = view_angles();
        const float yaw = view.y * (3.14159265f / 180.f);
        const float cy = std::cos(yaw);
        const float sy = std::sin(yaw);
        const float fdot = vel.x * cy + vel.y * sy;
        const float sdot = vel.x * sy - vel.y * cy;
        fwd = (fdot < -8.f) ? 1.f : (fdot > 8.f) ? -1.f : 0.f;
        left = (sdot > 8.f) ? 1.f : (sdot < -8.f) ? -1.f : 0.f;
    }

    inline void set_wish_move(uintptr_t pawn, float fwd, float left) {
        if (!mem::valid(pawn)) return;
        if (!std::isfinite(fwd) || !std::isfinite(left)) return;
        if (fwd > 1.f) fwd = 1.f;
        if (fwd < -1.f) fwd = -1.f;
        if (left > 1.f) left = 1.f;
        if (left < -1.f) left = -1.f;
        const auto ms = mem::read<uintptr_t>(pawn + schema::C_BasePlayerPawn::m_pMovementServices);
        if (!mem::valid(ms)) return;
        const auto client = client_base();
        const auto vt = mem::read<uintptr_t>(ms);
        if (!client || vt < client || vt >= client + 0x08000000ull)
            return;
        const float cur_f = mem::read<float>(ms + schema::CPlayer_MovementServices::m_flForwardMove);
        const float cur_l = mem::read<float>(ms + schema::CPlayer_MovementServices::m_flLeftMove);
        if (!std::isfinite(cur_f) || !std::isfinite(cur_l)
            || std::fabs(cur_f) > 1.5f || std::fabs(cur_l) > 1.5f)
            return;
        mem::write<float>(ms + schema::CPlayer_MovementServices::m_flCmdForwardMove, fwd);
        mem::write<float>(ms + schema::CPlayer_MovementServices::m_flCmdLeftMove, left);
        mem::write<float>(ms + schema::CPlayer_MovementServices::m_flCmdUpMove, 0.f);
        mem::write<float>(ms + schema::CPlayer_MovementServices::m_flForwardMove, fwd);
        mem::write<float>(ms + schema::CPlayer_MovementServices::m_flLeftMove, left);
        mem::write<float>(ms + schema::CPlayer_MovementServices::m_flUpMove, 0.f);
    }

    inline bool local_alive() {
        const auto pawn = local_pawn();
        if (!mem::valid(pawn)) return false;
        if (mem::read<int>(pawn + schema::C_BaseEntity::m_iHealth) <= 0) return false;
        return mem::read<uint8_t>(pawn + schema::C_BaseEntity::m_lifeState) == 0;
    }

    inline bool net_connected() {
        const auto eng = modules::engine2();
        if (!eng) return false;
        return mem::valid(mem::read<uintptr_t>(
            eng + schema::off::engine2_dll::dwNetworkGameClient));
    }

    // Deconnexion reelle (menu principal): le client reseau est detruit.
    inline bool session_connected() {
        return net_connected();
    }

    inline bool world_ready() {
        if (!client_base()) return false;
        if (!session_connected()) return false;
        const auto pawn = local_pawn();
        if (!mem::valid(pawn)) return false;
        if (!mem::valid(entity_list())) return false;
        const auto vm = view_matrix();
        return std::fabs(vm.m[0][0]) > 0.0001f || std::fabs(vm.m[3][3]) > 0.0001f;
    }

    inline void set_view_angles(const Vec3& a) {
        Vec3 v = a;
        normalize_angles(v);
        const auto addr = view_angles_addr();
        if (addr) mem::write<Vec3>(addr, v);
        const auto dump = client_base() + schema::off::client_dll::dwViewAngles;
        if (dump && dump != addr) mem::write<Vec3>(dump, v);
    }

    inline std::vector<Player> collect_players(bool include_teammates, CollectStats* stats = nullptr) {
        std::vector<Player> out;
        CollectStats local_stats{};
        if (!stats) stats = &local_stats;
        stats->list = entity_list();
        const auto local = local_pawn();
        const int local_team = local ? mem::read<uint8_t>(local + schema::C_BaseEntity::m_iTeamNum) : 0;

        for (int i = 1; i <= 64; ++i) {
            ++stats->scanned;
            const auto controller = get_entity(static_cast<uint32_t>(i));
            if (!mem::valid(controller)) continue;
            ++stats->controllers;

            const auto handle = mem::read<uint32_t>(controller + schema::CCSPlayerController::m_hPlayerPawn);
            if (!handle || handle == 0xFFFFFFFFu) continue;

            const auto pawn = get_entity(handle);
            if (!mem::valid(pawn) || pawn == local) continue;
            ++stats->pawns;

            const int hp = mem::read<int>(pawn + schema::C_BaseEntity::m_iHealth);
            const uint8_t life = mem::read<uint8_t>(pawn + schema::C_BaseEntity::m_lifeState);
            if (hp <= 0 || hp > 200 || life != 0) continue;

            const int team = mem::read<uint8_t>(pawn + schema::C_BaseEntity::m_iTeamNum);
            const bool teammate = local_team >= 2 && team >= 2 && team == local_team;
            if (teammate && !include_teammates) continue;

            Player p{};
            p.controller = controller;
            p.pawn = pawn;
            p.health = hp;
            p.team = team;
            p.teammate = teammate;
            p.steamid = mem::read<uint64_t>(controller + schema::CBasePlayerController::m_steamID);
            // Au changement de manche les pawns sont recrees: le scene node ou
            // le bone array peut etre invalide pendant 1-2 frames.
            const auto scene_node = mem::read<uintptr_t>(pawn + schema::C_BaseEntity::m_pGameSceneNode);
            if (!mem::valid(scene_node)) continue;

            p.origin = abs_origin(pawn);
            if (p.origin.length() < 1.f) continue;
            if (!std::isfinite(p.origin.x) || !std::isfinite(p.origin.z)) continue;

            if (!resolve_skeleton(pawn, p.head, p.feet, p.bone_lift)) {
                p.head = p.origin + Vec3{ 0, 0, 72 };
                p.feet = p.origin;
            }

            char name[128]{};
            mem::read_raw(controller + schema::CBasePlayerController::m_iszPlayerName, name, 127);
            p.name = name;
            ++stats->valid;
            out.push_back(std::move(p));
        }
        return out;
    }
}
