#include "features/bomb.hpp"
#include "sdk/entity.hpp"
#include "imgui.h"
#include <cmath>
#include <cstdio>

namespace features {
    static constexpr float k_defuse_no_kit = 10.f;
    static constexpr float k_defuse_kit = 5.f;

    static uintptr_t resolve_c4() {
        const auto client = game::client_base();
        if (!client) return 0;
        auto p = mem::read<uintptr_t>(client + schema::off::client_dll::dwPlantedC4);
        uintptr_t cands[4]{};
        int n = 0;
        auto add = [&](uintptr_t x) {
            if (!mem::valid(x) || n >= 4) return;
            for (int i = 0; i < n; ++i) {
                if (cands[i] == x) return;
            }
            cands[n++] = x;
        };
        add(p);
        if (mem::valid(p)) {
            // Tronquer un pointeur 64 bits en index d'entite designait une entite au
            // hasard, ensuite relue comme une C_PlantedC4.
            add(mem::read<uintptr_t>(p));
        }
        for (int i = 0; i < n; ++i) {
            const auto e = cands[i];
            if (mem::read<uint8_t>(e + schema::C_PlantedC4::m_bHasExploded))
                continue;
            if (mem::read<uint8_t>(e + schema::C_PlantedC4::m_bBombDefused))
                continue;
            const uint8_t tick = mem::read<uint8_t>(e + schema::C_PlantedC4::m_bBombTicking);
            const int site = mem::read<int>(e + schema::C_PlantedC4::m_nBombSite);
            const float len = mem::read<float>(e + schema::C_PlantedC4::m_flTimerLength);
            const float blow = mem::read<float>(e + schema::C_PlantedC4::m_flC4Blow);
            const bool site_ok = site == 0 || site == 1;
            const bool len_ok = std::isfinite(len) && len >= 9.f && len <= 45.f;
            const bool blow_ok = std::isfinite(blow) && blow > 1.f;
            if (tick == 1 || (site_ok && (len_ok || blow_ok)))
                return e;
        }
        return 0;
    }

    static float game_curtime() {
        auto gv = mem::read<uintptr_t>(game::client_base() + schema::off::client_dll::dwGlobalVars);
        if (!mem::valid(gv))
            return 0.f;
        const float a = mem::read<float>(gv + 0x30);
        const float b = mem::read<float>(gv + 0x2C);
        if (std::isfinite(a) && a > 1.f && a < 1.0e7f) return a;
        if (std::isfinite(b) && b > 1.f && b < 1.0e7f) return b;
        return 0.f;
    }

    static float time_left(uintptr_t bomb) {
        const float blow = mem::read<float>(bomb + schema::C_PlantedC4::m_flC4Blow);
        const float len = mem::read<float>(bomb + schema::C_PlantedC4::m_flTimerLength);
        const float cur = game_curtime();
        if (cur > 0.f && std::isfinite(blow)) {
            const float rem = blow - cur;
            if (rem >= 0.f && rem <= 45.f)
                return rem;
        }
        static uintptr_t last = 0;
        static DWORD t0 = 0;
        if (bomb != last) {
            last = bomb;
            t0 = GetTickCount();
        }
        float total = (std::isfinite(len) && len >= 9.f && len <= 45.f) ? len : 40.f;
        float left = total - (GetTickCount() - t0) / 1000.f;
        if (left < 0.f) left = 0.f;
        if (left > 45.f) left = 45.f;
        return left;
    }

    static bool pawn_has_kit(uintptr_t pawn) {
        if (!mem::valid(pawn)) return false;
        const auto items = mem::read<uintptr_t>(pawn + schema::C_BasePlayerPawn::m_pItemServices);
        if (!mem::valid(items)) return false;
        return mem::read<uint8_t>(items + schema::CCSPlayer_ItemServices::m_bHasDefuser) != 0;
    }

    void render_bomb_timer(int screen_w, int screen_h) {
        (void)screen_w;
        const auto bomb = resolve_c4();
        if (!bomb) return;

        const float left = time_left(bomb);
        const int site = mem::read<int>(bomb + schema::C_PlantedC4::m_nBombSite);
        const bool blocked = mem::read<uint8_t>(bomb + schema::C_PlantedC4::m_bCannotBeDefused) != 0;
        const bool defusing = mem::read<uint8_t>(bomb + schema::C_PlantedC4::m_bBeingDefused) != 0;
        const bool can_no_kit = !blocked && left >= k_defuse_no_kit;
        const bool can_kit = !blocked && left >= k_defuse_kit;

        bool defuse_ok = false;
        float defuse_left = 0.f;
        if (defusing) {
            const float cur = game_curtime();
            const float end = mem::read<float>(bomb + schema::C_PlantedC4::m_flDefuseCountDown);
            const float len = mem::read<float>(bomb + schema::C_PlantedC4::m_flDefuseLength);
            if (cur > 0.f && std::isfinite(end) && end > 1.f) {
                defuse_left = end - cur;
                if (defuse_left < 0.f) defuse_left = 0.f;
                defuse_ok = defuse_left <= left + 0.05f;
            } else if (std::isfinite(len) && len > 0.f && len <= 12.f) {
                defuse_left = len;
                defuse_ok = len <= left + 0.05f;
            }
        }

        const bool local_kit = pawn_has_kit(game::local_pawn());
        const bool local_can = !blocked && (local_kit ? can_kit : can_no_kit);

        char line1[64]{};
        char line2[64]{};
        char line3[64]{};
        char line4[64]{};
        std::snprintf(line1, sizeof(line1), "BOMBE  SITE %c   %.1fs",
            site == 0 ? 'A' : 'B', left);
        if (blocked) {
            std::snprintf(line2, sizeof(line2), "Sans kit   INDEFUSABLE");
            std::snprintf(line3, sizeof(line3), "Avec kit   INDEFUSABLE");
        } else {
            std::snprintf(line2, sizeof(line2), "Sans kit   %s", can_no_kit ? "OK" : "EXPLOSE");
            std::snprintf(line3, sizeof(line3), "Avec kit   %s", can_kit ? "OK" : "EXPLOSE");
        }
        if (defusing) {
            std::snprintf(line4, sizeof(line4), "Defuse     %s  %.1fs",
                defuse_ok ? "REUSSIT" : "RATE", defuse_left);
        } else if (!can_kit) {
            std::snprintf(line4, sizeof(line4), "VA EXPLOSER");
        } else if (!can_no_kit) {
            std::snprintf(line4, sizeof(line4), "Kit obligatoire%s",
                local_kit ? "" : "  (toi sans kit)");
        } else {
            std::snprintf(line4, sizeof(line4), "Toi        %s",
                local_can ? (local_kit ? "kit OK" : "sans kit OK") : "trop tard");
        }

        auto* dl = ImGui::GetForegroundDrawList();
        const float x = 18.f;
        const float y = screen_h * 0.40f;
        const float bw = 268.f;
        const float bh = 92.f;
        ImU32 col = IM_COL32(80, 220, 140, 255);
        if (blocked || (!can_kit && !defuse_ok))
            col = IM_COL32(255, 70, 90, 255);
        else if (!can_no_kit)
            col = IM_COL32(255, 170, 60, 255);
        else if (defusing && !defuse_ok)
            col = IM_COL32(255, 70, 90, 255);

        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), IM_COL32(18, 22, 28, 230), 6.f);
        dl->AddRect(ImVec2(x, y), ImVec2(x + bw, y + bh), col, 6.f, 0, 1.5f);
        dl->AddText(ImVec2(x + 10.f, y + 8.f), col, line1);
        dl->AddText(ImVec2(x + 10.f, y + 28.f),
            can_no_kit ? IM_COL32(80, 220, 140, 255) : IM_COL32(255, 90, 110, 255), line2);
        dl->AddText(ImVec2(x + 10.f, y + 46.f),
            can_kit ? IM_COL32(80, 220, 140, 255) : IM_COL32(255, 90, 110, 255), line3);
        dl->AddText(ImVec2(x + 10.f, y + 66.f), col, line4);
    }
}
