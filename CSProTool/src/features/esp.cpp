#include "features/esp.hpp"
#include "features/misc.hpp"
#include "sdk/entity.hpp"
#include "sdk/spread.hpp"
#include "sdk/weapons.hpp"
#include "features/bomb.hpp"
#include "features/avatars.hpp"
#include "hooks/createmove.hpp"
#include "hooks/chamsdraw.hpp"
#include "core/status.hpp"
#include "imgui.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace features {
    static ImU32 col_esp(int idx, bool teammate) {
        if (teammate && !cfg::visuals::rainbow)
            return IM_COL32(180, 180, 180, 255);
        if (cfg::visuals::rainbow) {
            const float t = (float)ImGui::GetTime() * 2.2f + idx * 0.11f;
            const int r = (int)((sinf(t) * 0.5f + 0.5f) * 255);
            const int g = (int)((sinf(t + 2.094f) * 0.5f + 0.5f) * 255);
            const int b = (int)((sinf(t + 4.188f) * 0.5f + 0.5f) * 255);
            return IM_COL32(r, g, b, 255);
        }
        return IM_COL32(
            (int)(cfg::visuals::esp_color[0] * 255),
            (int)(cfg::visuals::esp_color[1] * 255),
            (int)(cfg::visuals::esp_color[2] * 255),
            255);
    }

    static ImU32 hp_col(int hp) {
        if (hp > 60) return IM_COL32(46, 213, 115, 255);
        if (hp > 30) return IM_COL32(255, 165, 2, 255);
        return IM_COL32(255, 71, 87, 255);
    }

    static void corner_box(ImDrawList* dl, float x, float y, float w, float h, ImU32 col) {
        const float l = (std::min)(8.f, w * 0.25f);
        dl->AddLine(ImVec2(x, y), ImVec2(x + l, y), col, 1.4f);
        dl->AddLine(ImVec2(x, y), ImVec2(x, y + l), col, 1.4f);
        dl->AddLine(ImVec2(x + w, y), ImVec2(x + w - l, y), col, 1.4f);
        dl->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + l), col, 1.4f);
        dl->AddLine(ImVec2(x, y + h), ImVec2(x + l, y + h), col, 1.4f);
        dl->AddLine(ImVec2(x, y + h), ImVec2(x, y + h - l), col, 1.4f);
        dl->AddLine(ImVec2(x + w, y + h), ImVec2(x + w - l, y + h), col, 1.4f);
        dl->AddLine(ImVec2(x + w, y + h), ImVec2(x + w, y + h - l), col, 1.4f);
    }

    static ImU32 with_alpha(ImU32 c, int a) {
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        return (c & 0x00FFFFFFu) | (static_cast<ImU32>(a) << 24);
    }

    static ImU32 glow_col(int idx, bool teammate) {
        if (cfg::visuals::rainbow)
            return col_esp(idx, false);
        // Photo: alliés violet, ennemis = couleur menu (rouge neon par defaut).
        if (teammate)
            return IM_COL32(180, 50, 255, 255);
        return IM_COL32(
            (int)(cfg::visuals::glow_color[0] * 255),
            (int)(cfg::visuals::glow_color[1] * 255),
            (int)(cfg::visuals::glow_color[2] * 255),
            255);
    }

    static float limb_radius(int a, int b, float body_h) {
        const int lo = (std::min)(a, b);
        const int hi = (std::max)(a, b);
        if (lo == 7 && hi == 8) return body_h * 0.046f;
        if (lo == 6 && hi == 7) return body_h * 0.040f;
        if (lo <= 6 || hi == 23) return body_h * 0.052f;
        if (lo >= 9 && hi <= 15) return body_h * 0.024f;
        return body_h * 0.032f;
    }

    static ImVec2 perp_off(ImVec2 a, ImVec2 b, float r) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.5f) return ImVec2(r, 0.f);
        return ImVec2(-dy / len * r, dx / len * r);
    }

    static int convex_hull(ImVec2* pts, int n, ImVec2* out, int cap);

    static void draw_limb_quad(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImU32 fill, ImU32 rim) {
        if (r < 1.f) r = 1.f;
        const ImVec2 n = perp_off(a, b, r);
        ImVec2 q[4]{
            ImVec2(a.x + n.x, a.y + n.y),
            ImVec2(b.x + n.x, b.y + n.y),
            ImVec2(b.x - n.x, b.y - n.y),
            ImVec2(a.x - n.x, a.y - n.y),
        };
        dl->AddConvexPolyFilled(q, 4, fill);
        dl->AddPolyline(q, 4, rim, ImDrawFlags_Closed, 1.35f);
    }

    static bool entity_live(uintptr_t ent);

    // Glow silhouette en espace ecran: epaisseur en PIXELS fixe, donc identique
    // de pres comme de loin (le CGlowProperty moteur fait une aura qui explose a distance).
    static void draw_screen_glow(ImDrawList* dl, const ViewMatrix& vm, const game::Player& p,
                                 int sw, int sh, ImU32 col) {
        if (!entity_live(p.pawn))
            return;

        ImVec2 raw[96]{};
        int nraw = 0;
        auto push = [&](ImVec2 s) {
            if (nraw < 96) raw[nraw++] = s;
        };

        // Points de squelette + leger elargissement CONSTANT en pixels (pas % hauteur).
        constexpr float k_pad = 3.5f;
        for (const auto& link : game::k_bone_links) {
            Vec3 wa = game::bone_pos(p.pawn, link[0], p.bone_lift);
            Vec3 wb = game::bone_pos(p.pawn, link[1], p.bone_lift);
            if (wa.length() < 1.f || wb.length() < 1.f) continue;
            const float dist = (wa - wb).length();
            if (dist < 1.5f || dist > 70.f) continue;
            Vec2 sa{}, sb{};
            if (!vm.world_to_screen(wa, sa, sw, sh)) continue;
            if (!vm.world_to_screen(wb, sb, sw, sh)) continue;
            const ImVec2 a{ sa.x, sa.y }, b{ sb.x, sb.y };
            const ImVec2 n = perp_off(a, b, k_pad);
            push(ImVec2(a.x + n.x, a.y + n.y));
            push(ImVec2(a.x - n.x, a.y - n.y));
            push(ImVec2(b.x + n.x, b.y + n.y));
            push(ImVec2(b.x - n.x, b.y - n.y));
        }
        {
            Vec2 s{};
            if (vm.world_to_screen(p.head, s, sw, sh)) {
                for (int i = 0; i < 8; ++i) {
                    const float a = i * 3.14159265f * 0.25f;
                    push(ImVec2(s.x + std::cos(a) * (k_pad + 2.f),
                                s.y + std::sin(a) * (k_pad + 2.f)));
                }
            }
        }
        if (nraw < 3) return;

        ImVec2 hull[192]{};
        const int nh = convex_hull(raw, nraw, hull, 192);
        if (nh < 3) return;

        // Aura soft (px fixes) — vrai glow silhouette, stable de pres / loin.
        static const struct { float w; int a; } layers[] = {
            { 26.f, 10 },
            { 20.f, 16 },
            { 15.f, 24 },
            { 11.f, 36 },
            {  8.f, 55 },
            {  5.5f, 85 },
            {  3.5f, 130 },
            {  2.0f, 185 },
            {  1.0f, 230 },
        };
        for (const auto& L : layers)
            dl->AddPolyline(hull, nh, with_alpha(col, L.a), ImDrawFlags_Closed, L.w);

        dl->AddConvexPolyFilled(hull, nh, with_alpha(col, 28));
    }

    static void draw_ghost(ImDrawList* dl, const ViewMatrix& vm, const game::Player& p,
                           int sw, int sh, ImU32 col) {
        draw_screen_glow(dl, vm, p, sw, sh, col);
    }

    // La chaine monotone ecrit jusqu'a 2n points: sans cap elle deborde de out
    // et corrompt la pile de l'appelant.
    static int convex_hull(ImVec2* pts, int n, ImVec2* out, int cap) {
        if (n < 3 || cap < 3) return 0;
        auto cross = [](ImVec2 o, ImVec2 a, ImVec2 b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                if (pts[j].x < pts[i].x || (pts[j].x == pts[i].x && pts[j].y < pts[i].y)) {
                    const ImVec2 t = pts[i];
                    pts[i] = pts[j];
                    pts[j] = t;
                }
            }
        }
        int k = 0;
        for (int i = 0; i < n; ++i) {
            while (k >= 2 && cross(out[k - 2], out[k - 1], pts[i]) <= 0.f) --k;
            if (k >= cap) break;
            out[k++] = pts[i];
        }
        const int t = k + 1;
        for (int i = n - 2; i >= 0; --i) {
            while (k >= t && cross(out[k - 2], out[k - 1], pts[i]) <= 0.f) --k;
            if (k >= cap) break;
            out[k++] = pts[i];
        }
        return k > 0 ? k - 1 : 0;
    }

    static bool entity_live(uintptr_t ent) {
        if (!mem::valid(ent)) return false;
        const auto ident = mem::read<uintptr_t>(ent + schema::CEntityInstance::m_pEntity);
        if (!mem::valid(ident)) return false;
        if (mem::read<uintptr_t>(ident) != ent) return false;
        const auto node = mem::read<uintptr_t>(ent + schema::C_BaseEntity::m_pGameSceneNode);
        return mem::valid(node);
    }

    static uintptr_t game_rules() {
        auto gr = mem::read<uintptr_t>(game::client_base() + schema::off::client_dll::dwGameRules);
        if (!mem::valid(gr)) return 0;
        gr = mem::read<uintptr_t>(gr);
        return mem::valid(gr) ? gr : 0;
    }

    static bool item_glow_unsafe() {
        const auto gr = game_rules();
        if (!mem::valid(gr)) return false;
        if (mem::read<uint8_t>(gr + schema::C_CSGameRules::m_bFreezePeriod)) return true;
        return mem::read<uint8_t>(gr + schema::C_CSGameRules::m_bBombDropped) != 0;
    }

    // Glow moteur natif (CGlowProperty): le shader Source 2 dessine le contour.
    // Pas d'appel SetGlowEffect — on ecrit directement m_bGlowing / couleur / type.
    static void apply_engine_glow(uintptr_t ent, bool on, uint8_t r, uint8_t g, uint8_t b) {
        if (!mem::valid(ent) || !entity_live(ent))
            return;
        namespace Glow = schema::CGlowProperty;
        const auto gp = ent + schema::C_BaseModelEntity::m_Glow;
        __try {
            if (!on) {
                mem::write<uint8_t>(gp + Glow::m_bGlowing, 0);
                mem::write<int32_t>(gp + Glow::m_iGlowType, 0);
                return;
            }
            const uint8_t col[4] = { r, g, b, 255 };
            mem::write_raw(gp + Glow::m_glowColorOverride, col, 4);
            mem::write<float>(gp + Glow::m_fGlowColor + 0, r / 255.f);
            mem::write<float>(gp + Glow::m_fGlowColor + 4, g / 255.f);
            mem::write<float>(gp + Glow::m_fGlowColor + 8, b / 255.f);
            mem::write<int32_t>(gp + Glow::m_nGlowRange, 8000);
            mem::write<int32_t>(gp + Glow::m_nGlowRangeMin, 0);
            // Type 3 = outline glow moteur (contour lumineux, pas fill mesh).
            mem::write<int32_t>(gp + Glow::m_iGlowType, 3);
            mem::write<uint8_t>(gp + Glow::m_bFlashing, 0);
            mem::write<uint8_t>(gp + Glow::m_bEligibleForScreenHighlight, 1);
            mem::write<uint8_t>(gp + Glow::m_bGlowing, 1);
            mem::write<float>(ent + schema::C_BaseModelEntity::m_flGlowBackfaceMult, 1.f);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    static uintptr_t g_glow_pawns[64]{};
    static int g_glow_n = 0;

    static void restore_player_glow() {
        for (int i = 0; i < g_glow_n; ++i) {
            if (g_glow_pawns[i])
                apply_engine_glow(g_glow_pawns[i], false, 0, 0, 0);
            g_glow_pawns[i] = 0;
        }
        g_glow_n = 0;
    }

    static bool freeze_period() {
        const auto gr = game_rules();
        if (!mem::valid(gr)) return false;
        return mem::read<uint8_t>(gr + schema::C_CSGameRules::m_bFreezePeriod) != 0;
    }

    static uintptr_t g_item_glow[96]{};
    static int g_item_n = 0;
    static DWORD g_item_scan_at = 0;

    static bool name_has(const char* hay, const char* n) {
        if (!hay || !n || !*n) return false;
        for (const char* p = hay; *p; ++p) {
            size_t i = 0;
            for (; n[i] && p[i]; ++i) {
                char a = p[i], b = n[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
                if (a != b) break;
            }
            if (!n[i]) return true;
        }
        return false;
    }

    static bool read_label(uintptr_t p, char* out, size_t n) {
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

    static bool item_label(uintptr_t ent, char* out, size_t n) {
        if (game::designer_name(ent, out, n))
            return true;
        const auto ident = mem::read<uintptr_t>(ent + schema::CEntityInstance::m_pEntity);
        if (!mem::valid(ident)) return false;
        const auto klass = mem::read<uintptr_t>(ident + 0x8);
        if (!mem::valid(klass)) return false;
        static constexpr uintptr_t k_off[] = { 0x8, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38 };
        for (auto off : k_off) {
            if (read_label(mem::read<uintptr_t>(klass + off), out, n))
                return true;
        }
        return false;
    }

    static bool looks_like_weapon_name(const char* name) {
        if (!name || !*name) return false;
        if (name_has(name, "hud") || name_has(name, "viewmodel")
            || name_has(name, "player") || name_has(name, "projectile")
            || name_has(name, "chicken") || name_has(name, "spawn"))
            return false;
        return name_has(name, "weapon_") || name_has(name, "C_Weapon")
            || name_has(name, "C_DEagle") || name_has(name, "C_Knife")
            || name_has(name, "C_C4") || name_has(name, "C_PlantedC4")
            || name_has(name, "planted_c4") || name_has(name, "weapon_c4")
            || name_has(name, "C_Molotov") || name_has(name, "C_Flashbang")
            || name_has(name, "C_HEGrenade") || name_has(name, "C_SmokeGrenade")
            || name_has(name, "C_DecoyGrenade") || name_has(name, "C_Incendiary")
            || name_has(name, "hkp2000") || name_has(name, "deagle")
            || name_has(name, "glock") || name_has(name, "usp_silencer")
            || name_has(name, "knife");
    }

    static bool is_bomb_name(const char* name) {
        if (!name || !*name) return false;
        if (name_has(name, "projectile")) return false;
        return name_has(name, "planted_c4") || name_has(name, "C_PlantedC4")
            || name_has(name, "weapon_c4") || name_has(name, "C_C4");
    }

    static bool is_weapon_def(uint16_t d) {
        if (d == 0 || d >= 5000) return false;
        if (d >= 1 && d <= 64 && d != 57) return true;
        if (d >= 500 && d <= 800) return true;
        if (d == 68 || d == 70 || (d >= 80 && d <= 85)) return true;
        return false;
    }

    static uint16_t item_def(uintptr_t ent) {
        return mem::read<uint16_t>(ent + schema::C_EconEntity::m_AttributeManager
            + schema::C_AttributeContainer::m_Item
            + schema::C_EconItemView::m_iItemDefinitionIndex);
    }

    static bool item_initialized(uintptr_t ent) {
        return mem::read<uint8_t>(ent + schema::C_EconEntity::m_AttributeManager
            + schema::C_AttributeContainer::m_Item
            + schema::C_EconItemView::m_bInitialized) == 1;
    }

    static uintptr_t planted_c4() {
        auto p = mem::read<uintptr_t>(game::client_base() + schema::off::client_dll::dwPlantedC4);
        if (!mem::valid(p)) return 0;
        if (mem::read<uint8_t>(p + schema::C_PlantedC4::m_bBombTicking))
            return p;
        p = mem::read<uintptr_t>(p);
        if (mem::valid(p) && mem::read<uint8_t>(p + schema::C_PlantedC4::m_bBombTicking))
            return p;
        return 0;
    }

    static bool owner_is_living_pawn(uint32_t handle) {
        if (!handle || handle == 0xFFFFFFFFu)
            return false;
        const auto o = game::get_entity(handle);
        if (!mem::valid(o))
            return false;
        const int hp = mem::read<int>(o + schema::C_BaseEntity::m_iHealth);
        if (hp <= 0 || hp > 200)
            return false;
        char n[80]{};
        if (!item_label(o, n, sizeof(n)))
            return true;
        return name_has(n, "player") || name_has(n, "pawn") || name_has(n, "CCSPlayer");
    }

    static uintptr_t ent_from_ident(uintptr_t ident) {
        if (!mem::valid(ident)) return 0;
        const auto ent = mem::read<uintptr_t>(ident);
        if (!mem::valid(ent)) return 0;
        if (mem::read<uintptr_t>(ent + schema::CEntityInstance::m_pEntity) != ident)
            return 0;
        return ent;
    }

    static bool is_map_prop_name(const char* name) {
        if (!name || !*name) return false;
        return name_has(name, "prop_") || name_has(name, "prop_dynamic")
            || name_has(name, "prop_static") || name_has(name, "static_")
            || name_has(name, "func_") || name_has(name, "env_")
            || name_has(name, "light") || name_has(name, "world")
            || name_has(name, "decal") || name_has(name, "physics")
            || name_has(name, "breakable") || name_has(name, "door")
            || name_has(name, "button") || name_has(name, "trigger")
            || name_has(name, "overlay") || name_has(name, "beam")
            || name_has(name, "rope") || name_has(name, "cable")
            || name_has(name, "bracket") || name_has(name, "hinge")
            || name_has(name, "detail") || name_has(name, "clip")
            || name_has(name, "ladder") || name_has(name, "sign");
    }

    static bool item_bbox_ok(uintptr_t ent, bool bomb) {
        if (bomb) return true;
        auto col = mem::read<uintptr_t>(ent + schema::C_BaseEntity::m_pCollision);
        if (!mem::valid(col))
            col = ent + schema::C_BaseModelEntity::m_Collision;
        if (!mem::valid(col)) return true;
        const auto mn = mem::read<Vec3>(col + schema::CCollisionProperty::m_vecMins);
        const auto mx = mem::read<Vec3>(col + schema::CCollisionProperty::m_vecMaxs);
        if (!std::isfinite(mn.x) || !std::isfinite(mx.x)) return true;
        const float sz = (mx - mn).length();
        return sz >= 7.f && sz <= 72.f;
    }

    static bool weapon_pickup_ok(uintptr_t ent) {
        return mem::read<uint8_t>(ent + schema::C_CSWeaponBase::m_bCanBePickedUp) == 1;
    }

    static bool is_ground_item(uintptr_t ent, uintptr_t local, uintptr_t held) {
        if (!mem::valid(ent) || ent == local || ent == held)
            return false;
        char name[80]{};
        const bool named = item_label(ent, name, sizeof(name));
        if (named && (name_has(name, "hud") || name_has(name, "viewmodel")
            || name_has(name, "player") || name_has(name, "projectile")
            || name_has(name, "chicken") || is_map_prop_name(name)))
            return false;
        const bool bomb = (named && is_bomb_name(name))
            || (mem::read<uint8_t>(ent + schema::C_PlantedC4::m_bBombTicking) == 1
                && (mem::read<int>(ent + schema::C_PlantedC4::m_nBombSite) == 0
                    || mem::read<int>(ent + schema::C_PlantedC4::m_nBombSite) == 1)
                && mem::read<float>(ent + schema::C_PlantedC4::m_flTimerLength) >= 9.f
                && mem::read<float>(ent + schema::C_PlantedC4::m_flTimerLength) <= 45.f);
        const bool gun = item_initialized(ent) && is_weapon_def(item_def(ent));
        const bool named_gun = named && looks_like_weapon_name(name);
        if (!bomb && !gun && !named_gun)
            return false;
        if (!bomb) {
            if (!weapon_pickup_ok(ent))
                return false;
            if (!item_bbox_ok(ent, false))
                return false;
        }
        if (!bomb && owner_is_living_pawn(mem::read<uint32_t>(ent + schema::C_BaseEntity::m_hOwnerEntity)))
            return false;
        if (!bomb && mem::read<uint8_t>(ent + schema::C_CSWeaponBase::m_bUIWeapon) == 1)
            return false;
        const auto node = mem::read<uintptr_t>(ent + schema::C_BaseEntity::m_pGameSceneNode);
        if (!mem::valid(node))
            return false;
        const auto o = mem::read<Vec3>(node + schema::CGameSceneNode::m_vecAbsOrigin);
        if (!std::isfinite(o.x) || !std::isfinite(o.y) || !std::isfinite(o.z))
            return false;
        if (o.length() < 8.f || o.length() > 20000.f)
            return false;
        return true;
    }

    static bool is_dropped_c4(uintptr_t ent) {
        char name[80]{};
        if (!item_label(ent, name, sizeof(name)))
            return item_def(ent) == 49 && mem::read<uint8_t>(ent + schema::C_PlantedC4::m_bBombTicking) != 1;
        if (!is_bomb_name(name)) return false;
        if (name_has(name, "planted_c4") || name_has(name, "C_PlantedC4")) return false;
        return mem::read<uint8_t>(ent + schema::C_PlantedC4::m_bBombTicking) != 1;
    }

    static bool item_engine_glow_ok(uintptr_t ent) {
        if (!entity_live(ent)) return false;
        if (item_def(ent) == 49) return false;
        if (is_dropped_c4(ent)) return false;
        if (mem::read<uint8_t>(ent + schema::C_PlantedC4::m_bBombTicking) == 1)
            return false;
        char name[80]{};
        if (item_label(ent, name, sizeof(name)) && (is_bomb_name(name) || is_map_prop_name(name)))
            return false;
        if (!weapon_pickup_ok(ent) || !item_bbox_ok(ent, false))
            return false;
        if (item_initialized(ent) && is_weapon_def(item_def(ent)))
            return true;
        return item_label(ent, name, sizeof(name)) && looks_like_weapon_name(name);
    }

    static Vec3 rotate_by_angles(const Vec3& v, const Vec3& a) {
        const float d = 3.14159265f / 180.f;
        const float sp = sinf(a.x * d), cp = cosf(a.x * d);
        const float sy = sinf(a.y * d), cy = cosf(a.y * d);
        const float sr = sinf(a.z * d), cr = cosf(a.z * d);
        return {
            v.x * (cp * cy) + v.y * (sr * sp * cy - cr * sy) + v.z * (cr * sp * cy + sr * sy),
            v.x * (cp * sy) + v.y * (sr * sp * sy + cr * cy) + v.z * (cr * sp * sy - sr * cy),
            v.x * (-sp) + v.y * (sr * cp) + v.z * (cr * cp)
        };
    }

    static void inflate_hull(ImVec2* h, int n, float px) {
        if (n < 3) return;
        float cx = 0.f, cy = 0.f;
        for (int i = 0; i < n; ++i) { cx += h[i].x; cy += h[i].y; }
        cx /= (float)n;
        cy /= (float)n;
        for (int i = 0; i < n; ++i) {
            ImVec2 d{ h[i].x - cx, h[i].y - cy };
            const float l = sqrtf(d.x * d.x + d.y * d.y);
            if (l < 0.01f) continue;
            h[i].x += d.x / l * px;
            h[i].y += d.y / l * px;
        }
    }

    static void collect_item_pts(uintptr_t ent, const ViewMatrix& vm, int sw, int sh,
                                 ImVec2* pts, int& n, int cap) {
        const auto origin = game::abs_origin(ent);
        const auto node = mem::read<uintptr_t>(ent + schema::C_BaseEntity::m_pGameSceneNode);
        Vec3 ang{};
        if (mem::valid(node))
            ang = mem::read<Vec3>(node + schema::CGameSceneNode::m_angAbsRotation);

        auto push_w = [&](const Vec3& w) {
            if (n >= cap) return;
            if ((w - origin).length() > 70.f) return;
            if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z)) return;
            Vec2 s{};
            if (!vm.world_to_screen(w, s, sw, sh)) return;
            pts[n++] = ImVec2(s.x, s.y);
        };

        push_w(origin);

        Vec3 mins{ -12.f, -4.f, -3.5f }, maxs{ 12.f, 4.f, 3.5f };
        auto col = mem::read<uintptr_t>(ent + schema::C_BaseEntity::m_pCollision);
        if (!mem::valid(col))
            col = ent + schema::C_BaseModelEntity::m_Collision;
        if (mem::valid(col)) {
            const auto mn = mem::read<Vec3>(col + schema::CCollisionProperty::m_vecMins);
            const auto mx = mem::read<Vec3>(col + schema::CCollisionProperty::m_vecMaxs);
            if (std::isfinite(mn.x) && std::isfinite(mx.x)
                && (mx - mn).length() > 1.f && (mx - mn).length() < 70.f) {
                mins = mn;
                maxs = mx;
            }
        }
        mins = mins + Vec3{ -2.2f, -2.2f, -2.2f };
        maxs = maxs + Vec3{ 2.2f, 2.2f, 2.2f };

        const auto bones = game::bone_array_ptr(ent);
        const float bone_lim = (std::max)(18.f, (maxs - mins).length() * 0.85f);
        const bool skip_bones = item_def(ent) == 49
            || mem::read<uint8_t>(ent + schema::C_PlantedC4::m_bBombTicking) == 1;
        if (!skip_bones && mem::valid(bones)) {
            for (int i = 0; i < 12; ++i) {
                const auto b = game::bone_raw(bones, i);
                if (b.length() < 1.f) continue;
                if ((b - origin).length() <= bone_lim)
                    push_w(b);
                else if (b.length() <= bone_lim)
                    push_w(origin + b);
            }
        }

        for (int ix = 0; ix < 3; ++ix) {
            for (int iy = 0; iy < 3; ++iy) {
                for (int iz = 0; iz < 3; ++iz) {
                    const Vec3 local{
                        mins.x + (maxs.x - mins.x) * (0.5f * ix),
                        mins.y + (maxs.y - mins.y) * (0.5f * iy),
                        mins.z + (maxs.z - mins.z) * (0.5f * iz)
                    };
                    push_w(origin + rotate_by_angles(local, ang));
                }
            }
        }
    }

    static void draw_item_outline(ImDrawList* dl, const ImVec2* hull, int n, ImU32 col) {
        const ImU32 dim = (col & 0x00FFFFFFu) | 0x46000000u;
        const ImU32 mid = (col & 0x00FFFFFFu) | 0x8C000000u;
        if (n < 3) {
            if (n == 1) {
                dl->AddCircleFilled(hull[0], 7.f, dim);
                dl->AddCircle(hull[0], 8.f, col, 14, 1.8f);
            } else if (n == 2) {
                dl->AddLine(hull[0], hull[1], col, 2.f);
            }
            return;
        }
        dl->AddPolyline(hull, n, dim, ImDrawFlags_Closed, 6.5f);
        dl->AddPolyline(hull, n, mid, ImDrawFlags_Closed, 3.4f);
        dl->AddPolyline(hull, n, col, ImDrawFlags_Closed, 1.6f);
    }

    static void restore_item_glow() {
        for (int i = 0; i < g_item_n; ++i) {
            if (g_item_glow[i])
                apply_engine_glow(g_item_glow[i], false, 0, 0, 0);
            g_item_glow[i] = 0;
        }
        g_item_n = 0;
        g_item_scan_at = 0;
    }

    static void scan_ground_items(uintptr_t local, uintptr_t held) {
        int highest = 8192;
        const auto sys = game::entity_list();
        if (mem::valid(sys)) {
            const int h = mem::read<int>(sys + schema::off::client_dll::dwGameEntitySystem_highestEntityIndex);
            if (h > 64 && h < 0x8000)
                highest = h + 8;
        }

        uintptr_t now[96]{};
        int nn = 0;
        auto push = [&](uintptr_t ent) {
            if (!mem::valid(ent) || !is_ground_item(ent, local, held) || nn >= 96) return;
            for (int i = 0; i < nn; ++i) {
                if (now[i] == ent) return;
            }
            now[nn++] = ent;
        };

        for (int i = 1; i < highest; ++i)
            push(game::get_entity(static_cast<uint32_t>(i)));

        const auto bomb_ent = planted_c4();
        if (mem::valid(bomb_ent))
            push(bomb_ent);

        g_item_n = nn;
        for (int i = 0; i < nn; ++i)
            g_item_glow[i] = now[i];

        if (item_glow_unsafe())
            return;

        for (int i = 0; i < nn; ++i) {
            if (item_engine_glow_ok(now[i]))
                apply_engine_glow(now[i], true, 255, 255, 255);
        }
    }

    static void tick_item_glow(ImDrawList* dl, const ViewMatrix& vm, int sw, int sh) {
        if (!cfg::visuals::item_glow) {
            g_item_n = 0;
            return;
        }
        const auto local = game::local_pawn();
        const auto held = mem::valid(local) ? game::active_weapon(local) : 0;
        const DWORD t = GetTickCount();
        if (g_item_n == 0 || t - g_item_scan_at > 120) {
            g_item_scan_at = t;
            scan_ground_items(local, held);
        }

        const auto bomb_ent = planted_c4();
        for (int i = 0; i < g_item_n; ++i) {
            const auto ent = g_item_glow[i];
            if (!entity_live(ent))
                continue;
            char name[80]{};
            item_label(ent, name, sizeof(name));
            const bool bomb = (bomb_ent && ent == bomb_ent) || is_bomb_name(name) || item_def(ent) == 49;
            ImVec2 pts[48]{};
            int np = 0;
            collect_item_pts(ent, vm, sw, sh, pts, np, 48);
            const ImU32 col = bomb ? IM_COL32(255, 90, 200, 235) : IM_COL32(255, 255, 255, 235);
            if (np >= 3) {
                ImVec2 hull[96]{};
                const int hn = convex_hull(pts, np, hull, 96);
                inflate_hull(hull, hn, bomb ? 2.2f : 1.5f);
                draw_item_outline(dl, hull, hn, col);
            } else {
                draw_item_outline(dl, pts, np, col);
            }
            if (bomb && cfg::visuals::bomb_timer) {
                float cx = 0.f, cy = 0.f;
                int nlab = 0;
                for (int k = 0; k < np; ++k) {
                    cx += pts[k].x;
                    cy += pts[k].y;
                    ++nlab;
                }
                if (nlab > 0) {
                    cx /= static_cast<float>(nlab);
                    cy /= static_cast<float>(nlab);
                    const int site = mem::read<int>(ent + schema::C_PlantedC4::m_nBombSite);
                    char lab[24]{};
                    std::snprintf(lab, sizeof(lab), "C4  SITE %c", site == 0 ? 'A' : 'B');
                    dl->AddText(ImVec2(cx - 28.f, cy - 18.f), col, lab);
                }
            }
        }
    }

    static bool w2s_far(const ViewMatrix& vm, const Vec3& world, Vec2& screen, int width, int height) {
        const float w = vm.m[3][0] * world.x + vm.m[3][1] * world.y + vm.m[3][2] * world.z + vm.m[3][3];
        if (w < 0.001f)
            return false;
        const float inv = 1.0f / w;
        const float x = (vm.m[0][0] * world.x + vm.m[0][1] * world.y + vm.m[0][2] * world.z + vm.m[0][3]) * inv;
        const float y = (vm.m[1][0] * world.x + vm.m[1][1] * world.y + vm.m[1][2] * world.z + vm.m[1][3]) * inv;
        screen.x = (width * 0.5f) * (1.0f + x);
        screen.y = (height * 0.5f) * (1.0f - y);
        return true;
    }

    static bool edge_pos(const ViewMatrix& vm, const Vec3& world, int sw, int sh,
                         ImVec2& out, float& ang) {
        const float w = vm.m[3][0] * world.x + vm.m[3][1] * world.y + vm.m[3][2] * world.z + vm.m[3][3];
        const float x = vm.m[0][0] * world.x + vm.m[0][1] * world.y + vm.m[0][2] * world.z + vm.m[0][3];
        const float y = vm.m[1][0] * world.x + vm.m[1][1] * world.y + vm.m[1][2] * world.z + vm.m[1][3];
        float dx, dy;
        if (w < 0.02f) {
            dx = -x;
            dy = y;
        } else {
            const float inv = 1.f / w;
            dx = (sw * 0.5f) * (1.f + x * inv) - sw * 0.5f;
            dy = (sh * 0.5f) * (1.f - y * inv) - sh * 0.5f;
        }
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.001f)
            return false;
        dx /= len;
        dy /= len;
        const float margin = 26.f;
        const float hw = sw * 0.5f - margin;
        const float hh = sh * 0.5f - margin;
        const float tx = (std::fabs(dx) < 0.001f) ? 1.0e6f : hw / std::fabs(dx);
        const float ty = (std::fabs(dy) < 0.001f) ? 1.0e6f : hh / std::fabs(dy);
        const float t = (std::min)(tx, ty);
        out = ImVec2(sw * 0.5f + dx * t, sh * 0.5f + dy * t);
        ang = std::atan2(dy, dx);
        return true;
    }

    static bool box_on_screen(float bx, float by, float bw, float bh, int sw, int sh) {
        const float cx = bx + bw * 0.5f;
        const float cy = by + bh * 0.35f;
        return cx > 20.f && cx < sw - 20.f && cy > 20.f && cy < sh - 20.f;
    }

    static void draw_offscreen(ImDrawList* dl, const ViewMatrix& vm, const game::Player& p,
                              int sw, int sh, ImU32 col) {
        Vec3 aim = p.head.length() > 1.f ? p.head : p.origin;
        if (aim.length() < 1.f)
            aim = p.feet;
        ImVec2 pos{};
        float ang = 0.f;
        if (!edge_pos(vm, aim, sw, sh, pos, ang))
            return;
        const float s = 10.f;
        const ImVec2 a{ pos.x + std::cos(ang) * s, pos.y + std::sin(ang) * s };
        const ImVec2 b{ pos.x + std::cos(ang + 2.45f) * s * 0.72f, pos.y + std::sin(ang + 2.45f) * s * 0.72f };
        const ImVec2 c{ pos.x + std::cos(ang - 2.45f) * s * 0.72f, pos.y + std::sin(ang - 2.45f) * s * 0.72f };
        dl->AddTriangleFilled(a, b, c, with_alpha(col, 230));
        dl->AddTriangle(a, b, c, with_alpha(col, 255), 1.1f);
    }

    static void draw_limb(ImDrawList* dl, const ViewMatrix& vm, uintptr_t pawn, float lift,
                          int a, int b, int w, int h, ImU32 col) {
        Vec3 wa = game::bone_pos(pawn, a, lift);
        Vec3 wb = game::bone_pos(pawn, b, lift);
        if (wa.length() < 1.f || wb.length() < 1.f) return;
        const float dist = (wa - wb).length();
        if (dist < 1.5f || dist > 70.f) return;
        Vec2 sa{}, sb{};
        if (!vm.world_to_screen(wa, sa, w, h)) return;
        if (!vm.world_to_screen(wb, sb, w, h)) return;
        dl->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), col, 1.2f);
    }

    static bool project_box(const ViewMatrix& vm, const game::Player& p, int sw, int sh,
                            float& left, float& top, float& width, float& height) {
        const Vec3 head_top{ p.head.x, p.head.y, p.head.z + 12.f };
        Vec2 head2d{}, feet2d{}, top2d{};
        if (!w2s_far(vm, p.head, head2d, sw, sh)) return false;
        if (!w2s_far(vm, p.feet, feet2d, sw, sh)) return false;

        float min_x = (std::min)(head2d.x, feet2d.x);
        float max_x = (std::max)(head2d.x, feet2d.x);
        float min_y = (std::min)(head2d.y, feet2d.y);
        float max_y = (std::max)(head2d.y, feet2d.y);

        if (w2s_far(vm, head_top, top2d, sw, sh))
            min_y = (std::min)(min_y, top2d.y);

        static constexpr int expand_ids[] = { 1, 6, 7, 8, 9, 11, 13, 15, 19, 22 };
        for (int id : expand_ids) {
            Vec3 b = game::bone_pos(p.pawn, id, p.bone_lift);
            if (b.length() < 1.f) continue;
            Vec2 s{};
            if (!w2s_far(vm, b, s, sw, sh)) continue;
            min_x = (std::min)(min_x, s.x);
            max_x = (std::max)(max_x, s.x);
            min_y = (std::min)(min_y, s.y);
            max_y = (std::max)(max_y, s.y);
        }

        if (max_x < -40.f || min_x > sw + 40.f || max_y < -40.f || min_y > sh + 40.f)
            return false;

        float body_h = (std::max)(max_y - min_y, 24.f);
        float bone_w = (std::max)(max_x - min_x, 8.f);
        float body_w = (std::max)((std::max)(bone_w * 1.08f, body_h * 0.32f), 14.f);
        float cx = (min_x + max_x) * 0.5f;

        left = cx - body_w * 0.5f;
        top = min_y;
        width = body_w;
        height = body_h;
        // Un bone corrompu produit un NaN qui se propage jusqu'aux appels ImGui.
        return std::isfinite(left) && std::isfinite(top)
            && std::isfinite(width) && std::isfinite(height);
    }

    static bool observer_watching(uintptr_t ent, uintptr_t local_pawn, uintptr_t local_obs, uintptr_t local_ctrl) {
        if (!mem::valid(ent))
            return false;
        const auto svc = mem::read<uintptr_t>(ent + schema::C_BasePlayerPawn::m_pObserverServices);
        if (!mem::valid(svc))
            return false;
        const uint8_t mode = mem::read<uint8_t>(svc + schema::CPlayer_ObserverServices::m_iObserverMode);
        if (mode == 0 || mode > 7)
            return false;
        const uint32_t th = mem::read<uint32_t>(svc + schema::CPlayer_ObserverServices::m_hObserverTarget);
        if (!th || th == 0xFFFFFFFFu)
            return false;
        const auto tgt = game::get_entity(th);
        if (!mem::valid(tgt))
            return false;
        return tgt == local_pawn || tgt == local_obs || tgt == local_ctrl;
    }

    static void render_spec_list(int screen_w) {
        if (!cfg::visuals::spec_list)
            return;
        const auto client = game::client_base();
        if (!client)
            return;
        const auto local_ctrl = mem::read<uintptr_t>(
            client + schema::off::client_dll::dwLocalPlayerController);
        const auto local_pawn = game::local_pawn();
        if (!mem::valid(local_ctrl) && !mem::valid(local_pawn))
            return;

        uintptr_t local_obs = 0;
        if (mem::valid(local_ctrl)) {
            const uint32_t oh = mem::read<uint32_t>(local_ctrl + schema::CCSPlayerController::m_hObserverPawn);
            if (oh && oh != 0xFFFFFFFFu)
                local_obs = game::get_entity(oh);
        }

        char names[16][64]{};
        int n = 0;
        for (int i = 1; i <= 64 && n < 16; ++i) {
            const auto ctrl = game::get_entity(static_cast<uint32_t>(i));
            if (!mem::valid(ctrl) || ctrl == local_ctrl)
                continue;
            const uint32_t ph = mem::read<uint32_t>(ctrl + schema::CCSPlayerController::m_hPlayerPawn);
            const uint32_t oh = mem::read<uint32_t>(ctrl + schema::CCSPlayerController::m_hObserverPawn);
            const auto pawn = (ph && ph != 0xFFFFFFFFu) ? game::get_entity(ph) : 0;
            const auto obs = (oh && oh != 0xFFFFFFFFu) ? game::get_entity(oh) : 0;
            if (!observer_watching(pawn, local_pawn, local_obs, local_ctrl)
                && !observer_watching(obs, local_pawn, local_obs, local_ctrl))
                continue;
            char name[128]{};
            mem::read_raw(ctrl + schema::CBasePlayerController::m_iszPlayerName, name, 127);
            if (!name[0])
                continue;
            bool dup = false;
            for (int k = 0; k < n; ++k) {
                if (std::strcmp(names[k], name) == 0) { dup = true; break; }
            }
            if (dup)
                continue;
            std::snprintf(names[n], sizeof(names[n]), "%s", name);
            ++n;
        }

        auto* dl = ImGui::GetBackgroundDrawList();
        const float x = static_cast<float>(screen_w) - 228.f;
        float y = 16.f;
        char title[40]{};
        std::snprintf(title, sizeof(title), "SPECTATEURS  %d", n);
        dl->AddText(ImVec2(x, y), IM_COL32(245, 245, 245, 220), title);
        y += 16.f;
        if (n == 0) {
            dl->AddText(ImVec2(x, y), IM_COL32(140, 140, 140, 180), "personne");
            return;
        }
        for (int i = 0; i < n; ++i) {
            dl->AddText(ImVec2(x, y), IM_COL32(210, 210, 210, 220), names[i]);
            y += 15.f;
        }
    }

    static void render_esp_body(int screen_w, int screen_h) {
        auto* dl = ImGui::GetBackgroundDrawList();

        char st[160];
        const int aim = tools::last_aim.load(std::memory_order_relaxed);
        wsprintfA(st, "CS Pro | Aim:%s Rage:%s Silent:%s%s | CM:%s t=%u tgt=%d %s",
            cfg::combat::aimbot ? "ON" : "off",
            cfg::combat::rage ? "ON" : "off",
            cfg::combat::silent ? "ON" : "off",
            (cfg::combat::silent && cfg::combat::silent_360) ? "360" : "",
            hooks::createmove_active() ? "OK" : "--",
            hooks::createmove_ticks(),
            tools::last_targets.load(std::memory_order_relaxed),
            aim == 2 ? "SILENT" : (aim == 1 ? "SNAP" : ""));
        dl->AddText(ImVec2(16, 16), IM_COL32(245, 245, 245, 220), st);

        render_spec_list(screen_w);

        if (features::no_scope_crosshair()) {
            const float cx = screen_w * 0.5f;
            const float cy = screen_h * 0.5f;
            const float short_side = (screen_w < screen_h) ? (float)screen_w : (float)screen_h;
            const float gap = short_side * 0.042f;
            const float arm_h = gap * 2.6f;
            const float arm_v = gap * 2.1f;
            const ImU32 hair = IM_COL32(245, 245, 245, 230);
            const ImU32 shade = IM_COL32(0, 0, 0, 110);
            const ImU32 tint = IM_COL32(0, 0, 0, 48);

            dl->AddRectFilled(ImVec2(0.f, 0.f), ImVec2((float)screen_w, (float)screen_h), tint);

            auto stroke = [&](float x0, float y0, float x1, float y1) {
                dl->AddLine(ImVec2(x0 + 1.f, y0 + 1.f), ImVec2(x1 + 1.f, y1 + 1.f), shade, 1.f);
                dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), hair, 1.f);
            };
            stroke(cx - gap - arm_h, cy, cx - gap, cy);
            stroke(cx + gap, cy, cx + gap + arm_h, cy);
            stroke(cx, cy - gap - arm_v, cx, cy - gap);
            stroke(cx, cy + gap, cx, cy + gap + arm_v);
        }

        if (cfg::visuals::bomb_timer)
            features::render_bomb_timer(screen_w, screen_h);

        const auto vm = game::view_matrix();
        tick_item_glow(dl, vm, screen_w, screen_h);

        if (!cfg::visuals::esp) {
            restore_player_glow();
            return;
        }
        if (!cfg::visuals::glow)
            restore_player_glow();
        const bool include_mates = !cfg::combat::team_check || cfg::visuals::show_teammates;
        const auto players = game::collect_players(include_mates);

        if (cfg::visuals::fov_circle && !cfg::combat::rage && !cfg::combat::silent
            && cfg::combat::aimbot) {
            const ImU32 fc = IM_COL32(
                (int)(cfg::visuals::fov_color[0] * 255),
                (int)(cfg::visuals::fov_color[1] * 255),
                (int)(cfg::visuals::fov_color[2] * 255), 90);
            dl->AddCircle(ImVec2(screen_w * 0.5f, screen_h * 0.5f), cfg::combat::fov, fc, 64, 1.f);
        }

        const ImVec2 me{ screen_w * 0.5f, (float)screen_h - 1.f };
        int idx = 0;
        uintptr_t glow_now[64]{};
        uint8_t glow_rgb[64 * 3]{};
        int glow_nn = 0;

        if (cfg::visuals::glow)
            cham_draw::ensure_hooks();

        for (const auto& p : players) {
            // La liste est figee au debut de la frame: une cible tuee entre-temps
            // laisse un pointeur mort qu'on lirait ensuite pour ses bones.
            if (!entity_live(p.pawn)) { ++idx; continue; }

            float bx = 0.f, by = 0.f, bw = 0.f, bh = 0.f;
            const bool boxed = project_box(vm, p, screen_w, screen_h, bx, by, bw, bh);

            const bool mate = cfg::combat::team_check && p.teammate;
            const bool focused = cfg::combat::focus_id && game::player_id(p) == cfg::combat::focus_id;
            const ImU32 col = focused ? IM_COL32(255, 210, 70, 255) : col_esp(idx, mate);

            if (cfg::visuals::glow && entity_live(p.pawn)) {
                // Chams 3D mesh (comme avant) — corps plein colore.
                const ImU32 gc = glow_col(idx, mate);
                const uint8_t gr = static_cast<uint8_t>(gc & 255);
                const uint8_t gg = static_cast<uint8_t>((gc >> 8) & 255);
                const uint8_t gb = static_cast<uint8_t>((gc >> 16) & 255);
                if (glow_nn < 64) {
                    glow_now[glow_nn] = p.pawn;
                    glow_rgb[glow_nn * 3 + 0] = gr;
                    glow_rgb[glow_nn * 3 + 1] = gg;
                    glow_rgb[glow_nn * 3 + 2] = gb;
                    ++glow_nn;
                }
            }

            if (cfg::visuals::offscreen && (!boxed || !box_on_screen(bx, by, bw, bh, screen_w, screen_h)))
                draw_offscreen(dl, vm, p, screen_w, screen_h, col);

            if (!boxed) { ++idx; continue; }
            corner_box(dl, bx, by, bw, bh, col);

            const float ratio = (std::max)(0.f, (std::min)(1.f, p.health / 100.f));
            const float bar_h = bh * ratio;
            dl->AddRectFilled(ImVec2(bx - 5.f, by), ImVec2(bx - 3.f, by + bh), IM_COL32(0, 0, 0, 200));
            dl->AddRectFilled(ImVec2(bx - 5.f, by + (bh - bar_h)), ImVec2(bx - 3.f, by + bh), hp_col(p.health));

            if (cfg::visuals::hp_text) {
                char hp[8]{};
                std::snprintf(hp, sizeof(hp), "%d", p.health);
                dl->AddText(ImVec2(bx + bw + 4.f, by - 1.f), hp_col(p.health), hp);
            }

            float name_x = bx;
            const float name_y = by - 16.f;
            if (cfg::visuals::avatars && p.steamid) {
                avatars::request(p.steamid);
                ImTextureID tex{};
                int tw = 0, th = 0;
                if (avatars::get(p.steamid, &tex, &tw, &th) && tex) {
                    const float s = 14.f;
                    dl->AddImage(tex, ImVec2(name_x, name_y), ImVec2(name_x + s, name_y + s));
                    name_x += s + 4.f;
                }
            }
            if (cfg::visuals::names && !p.name.empty()) {
                dl->AddText(ImVec2(name_x, name_y),
                    focused ? IM_COL32(255, 210, 70, 255) : IM_COL32(255, 255, 255, 230),
                    p.name.c_str());
            }
            if (focused)
                dl->AddText(ImVec2(bx, name_y - 13.f), IM_COL32(255, 210, 70, 230), "FOCUS");

            if (cfg::visuals::weapon) {
                const auto wpn = game::active_weapon(p.pawn);
                const char* wn = mem::valid(wpn) ? weapons::name(spread::item_index(wpn)) : nullptr;
                if (wn)
                    dl->AddText(ImVec2(bx, by + bh + 1.f), IM_COL32(210, 210, 210, 220), wn);
            }

            if (cfg::visuals::snaplines) {
                const float mid_y = by + bh * 0.35f;
                dl->AddLine(me, ImVec2(bx + bw * 0.5f, mid_y), (col & 0x00FFFFFF) | 0xB4000000, 1.2f);
            }

            if (cfg::visuals::skeleton) {
                for (const auto& link : game::k_bone_links)
                    draw_limb(dl, vm, p.pawn, p.bone_lift, link[0], link[1], screen_w, screen_h, col);
            }
            ++idx;
        }

        if (cfg::visuals::glow) {
            cham_draw::publish_colored(glow_now, glow_rgb, glow_nn, true);
            // Mesh chams seul (retour v279) — pas de glow moteur joueur.
            if (g_glow_n > 0)
                restore_player_glow();
            g_glow_n = 0;
            for (int i = 0; i < glow_nn; ++i)
                g_glow_pawns[g_glow_n++] = glow_now[i];
        } else {
            cham_draw::publish(nullptr, 0, nullptr, 0, false, 0, 0, 0);
            restore_player_glow();
        }
    }

    // Le corps manipule des objets C++ (vector/string): le __try doit rester dans
    // une fonction sans variable a deroulement, d'ou ce wrapper.
    void render_esp(int screen_w, int screen_h) {
        __try {
            render_esp_body(screen_w, screen_h);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    void shutdown_esp() {
        restore_player_glow();
        restore_item_glow();
    }
}



