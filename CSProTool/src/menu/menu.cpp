#include "menu/menu.hpp"
#include "features/presets.hpp"
#include "features/skins.hpp"
#include "features/ragebot.hpp"
#include "sdk/entity.hpp"
#include "sdk/spread.hpp"
#include "core/memory.hpp"
#include "common.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "core/modules.hpp"
#include "core/status.hpp"
#include "core/version.hpp"
#include "hooks/createmove.hpp"
#include <string>
#include <cstdio>

namespace menu {
    namespace col {
        static ImVec4 rgba(int r, int g, int b, float a = 1.f) {
            return ImVec4(r / 255.f, g / 255.f, b / 255.f, a);
        }
        static const ImVec4 bg     = rgba(12, 12, 12, 0.90f);
        static const ImVec4 side   = rgba(8, 8, 8, 0.94f);
        static const ImVec4 card   = rgba(255, 255, 255, 0.035f);
        static const ImVec4 card_h = rgba(255, 255, 255, 0.06f);
        static const ImVec4 line   = rgba(255, 255, 255, 0.10f);
        static const ImVec4 text   = rgba(244, 244, 244);
        static const ImVec4 muted  = rgba(138, 138, 138);
        static const ImVec4 white  = rgba(255, 255, 255);
        static const ImVec4 dim    = rgba(255, 255, 255, 0.08f);
        static const ImVec4 ok     = rgba(74, 222, 128);
        static const ImVec4 danger = rgba(232, 120, 120);
    }

    static ImU32 u32(const ImVec4& c) {
        return ImGui::ColorConvertFloat4ToU32(c);
    }

    void setup_fonts() {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();
        ImFontConfig fc{};
        fc.OversampleH = 2;
        fc.OversampleV = 1;
        fc.PixelSnapH = true;

        const char* regular[] = {
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
        };
        const char* bold[] = {
            "C:\\Windows\\Fonts\\segoeuib.ttf",
            "C:\\Windows\\Fonts\\arialbd.ttf",
            "C:\\Windows\\Fonts\\segoeui.ttf",
        };
        auto add = [&](const char** paths, int n, float size, ImFont*& out) {
            for (int i = 0; i < n; ++i) {
                out = io.Fonts->AddFontFromFileTTF(paths[i], size, &fc);
                if (out) return;
            }
            out = io.Fonts->AddFontDefault();
        };
        add(regular, 2, 14.0f, font_body);
        add(regular, 2, 12.0f, font_small);
        add(bold, 3, 16.0f, font_title);
        add(bold, 3, 22.0f, font_hero);
        if (font_body) io.FontDefault = font_body;
    }

    static void apply_theme() {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding = 16.f;
        s.ChildRounding = 12.f;
        s.FrameRounding = 8.f;
        s.GrabRounding = 99.f;
        s.PopupRounding = 10.f;
        s.ScrollbarRounding = 8.f;
        s.TabRounding = 8.f;

        s.WindowBorderSize = 1.f;
        s.ChildBorderSize = 0.f;
        s.FrameBorderSize = 1.f;
        s.PopupBorderSize = 1.f;

        s.WindowPadding = ImVec2(0, 0);
        s.FramePadding = ImVec2(12, 8);
        s.ItemSpacing = ImVec2(8, 8);
        s.ItemInnerSpacing = ImVec2(8, 6);
        s.ScrollbarSize = 8.f;
        s.GrabMinSize = 14.f;

        ImVec4* c = s.Colors;
        c[ImGuiCol_Text] = col::text;
        c[ImGuiCol_TextDisabled] = col::muted;
        c[ImGuiCol_WindowBg] = col::bg;
        c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_PopupBg] = col::rgba(16, 16, 16, 0.96f);
        c[ImGuiCol_Border] = col::line;
        c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_FrameBg] = col::rgba(255, 255, 255, 0.04f);
        c[ImGuiCol_FrameBgHovered] = col::rgba(255, 255, 255, 0.07f);
        c[ImGuiCol_FrameBgActive] = col::rgba(255, 255, 255, 0.10f);
        c[ImGuiCol_TitleBg] = col::bg;
        c[ImGuiCol_TitleBgActive] = col::bg;
        c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ScrollbarGrab] = col::rgba(255, 255, 255, 0.14f);
        c[ImGuiCol_ScrollbarGrabHovered] = col::rgba(255, 255, 255, 0.24f);
        c[ImGuiCol_ScrollbarGrabActive] = col::white;
        c[ImGuiCol_CheckMark] = col::white;
        c[ImGuiCol_SliderGrab] = col::white;
        c[ImGuiCol_SliderGrabActive] = col::white;
        c[ImGuiCol_Button] = col::rgba(255, 255, 255, 0.04f);
        c[ImGuiCol_ButtonHovered] = col::rgba(255, 255, 255, 0.08f);
        c[ImGuiCol_ButtonActive] = col::rgba(255, 255, 255, 0.12f);
        c[ImGuiCol_Header] = col::dim;
        c[ImGuiCol_HeaderHovered] = col::rgba(255, 255, 255, 0.10f);
        c[ImGuiCol_HeaderActive] = col::rgba(255, 255, 255, 0.14f);
        c[ImGuiCol_Separator] = col::line;
        c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ResizeGripHovered] = col::dim;
        c[ImGuiCol_ResizeGripActive] = col::white;
        c[ImGuiCol_TextSelectedBg] = col::dim;
    }

    static void section(const char* title) {
        ImGui::Dummy(ImVec2(0, 10.f));
        if (font_small) ImGui::PushFont(font_small);
        ImGui::TextColored(col::muted, "%s", title);
        if (font_small) ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 4.f));
    }

    static int* g_bind_target = nullptr;
    static bool g_bind_armed = false;
    static const char* vk_label(int vk);

    static bool toggle_row(const char* label, bool* v) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        const ImGuiID id = window->GetID(label);
        const float full_w = ImGui::GetContentRegionAvail().x;
        const float row_h = 44.f;
        const float rnd = 10.f;
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + full_w, pos.y + row_h));

        ImGui::ItemSize(bb, 0.f);
        ImGui::Dummy(ImVec2(0, 6.f));
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        if (pressed) *v = !*v;

        ImDrawList* dl = window->DrawList;
        dl->AddRectFilled(bb.Min, bb.Max, u32(hovered ? col::card_h : col::card), rnd);
        dl->AddRect(bb.Min, bb.Max, u32(col::line), rnd, 0, 1.f);

        const ImVec2 tsz = ImGui::CalcTextSize(label);
        dl->AddText(
            ImVec2(bb.Min.x + 16.f, bb.Min.y + (row_h - tsz.y) * 0.5f),
            u32(col::text),
            label);

        const float sw = 38.f, sh = 20.f;
        const float sx = bb.Max.x - sw - 14.f;
        const float sy = bb.Min.y + (row_h - sh) * 0.5f;
        const ImU32 track = *v ? IM_COL32(255, 255, 255, 235) : IM_COL32(255, 255, 255, 28);
        dl->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + sw, sy + sh), track, sh * 0.5f);

        const float knob = 14.f;
        const float kx = *v ? (sx + sw - knob - 3.f) : (sx + 3.f);
        const float ky = sy + (sh - knob) * 0.5f;
        dl->AddCircleFilled(
            ImVec2(kx + knob * 0.5f, ky + knob * 0.5f),
            knob * 0.5f,
            *v ? IM_COL32(12, 12, 12, 255) : IM_COL32(230, 230, 230, 255));

        return pressed;
    }

    static void status_row(const char* name, const std::atomic<int>& st) {
        const int v = st.load(std::memory_order_relaxed);
        ImVec4 c = col::muted;
        if (v == static_cast<int>(tools::St::Ok)) c = col::ok;
        if (v == static_cast<int>(tools::St::Fail)) c = col::danger;

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const float full_w = ImGui::GetContentRegionAvail().x;
        const float row_h = 40.f;
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + full_w, pos.y + row_h));
        ImGui::ItemSize(bb);
        ImGui::Dummy(ImVec2(0, 6.f));
        ImDrawList* dl = window->DrawList;
        dl->AddRectFilled(bb.Min, bb.Max, u32(col::card), 10.f);
        dl->AddRect(bb.Min, bb.Max, u32(col::line), 10.f, 0, 1.f);
        dl->AddText(ImVec2(bb.Min.x + 16.f, bb.Min.y + (row_h - ImGui::GetFontSize()) * 0.5f),
            u32(col::text), name);
        const char* lab = tools::label(v);
        const ImVec2 lsz = ImGui::CalcTextSize(lab);
        dl->AddText(ImVec2(bb.Max.x - 16.f - lsz.x, bb.Min.y + (row_h - lsz.y) * 0.5f),
            u32(c), lab);
    }

    static void tiny_slider(const char* label, float* v, float mn, float mx) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const float full_w = ImGui::GetContentRegionAvail().x;
        const float box_h = 58.f;
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + full_w, pos.y + box_h));
        ImDrawList* dl = window->DrawList;
        dl->AddRectFilled(bb.Min, bb.Max, u32(col::card), 10.f);
        dl->AddRect(bb.Min, bb.Max, u32(col::line), 10.f, 0, 1.f);

        if (font_small) ImGui::PushFont(font_small);
        char cap[64]{};
        std::snprintf(cap, sizeof(cap), "%s   %.0f", label, *v);
        dl->AddText(ImVec2(bb.Min.x + 16.f, bb.Min.y + 10.f), u32(col::muted), cap);
        if (font_small) ImGui::PopFont();

        ImGui::SetCursorScreenPos(ImVec2(bb.Min.x + 16.f, bb.Min.y + 28.f));
        ImGui::PushItemWidth(full_w - 32.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        ImGui::SliderFloat(("##" + std::string(label)).c_str(), v, mn, mx, "");
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::PopItemWidth();
        ImGui::SetCursorScreenPos(ImVec2(pos.x, bb.Max.y + 6.f));
        ImGui::Dummy(ImVec2(0, 0));
    }

    static void color_row(const char* label, float* col4) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const float full_w = ImGui::GetContentRegionAvail().x;
        const float row_h = 44.f;
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + full_w, pos.y + row_h));
        ImDrawList* dl = window->DrawList;
        dl->AddRectFilled(bb.Min, bb.Max, u32(col::card), 10.f);
        dl->AddRect(bb.Min, bb.Max, u32(col::line), 10.f, 0, 1.f);
        dl->AddText(ImVec2(bb.Min.x + 16.f, bb.Min.y + (row_h - ImGui::GetFontSize()) * 0.5f),
            u32(col::text), label);
        ImGui::SetCursorScreenPos(ImVec2(bb.Max.x - 42.f, bb.Min.y + 10.f));
        ImGui::ColorEdit4(
            ("##c" + std::string(label)).c_str(),
            col4,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
        ImGui::SetCursorScreenPos(ImVec2(pos.x, bb.Max.y + 6.f));
        ImGui::Dummy(ImVec2(0, 0));
    }

    static void bind_row(const char* label, int* vk) {
        char caption[64]{};
        if (g_bind_target == vk)
            std::snprintf(caption, sizeof(caption), "Bind %s : ...", label);
        else
            std::snprintf(caption, sizeof(caption), "Bind %s : %s", label, vk_label(*vk));
        const std::string id = std::string(caption) + "##bind" + label;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16, 12));
        if (ImGui::Button(id.c_str(), ImVec2(-1.f, 42.f))) {
            g_bind_target = vk;
            g_bind_armed = false;
        }
        ImGui::PopStyleVar(2);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            *vk = 0;
            if (g_bind_target == vk) {
                g_bind_target = nullptr;
                g_bind_armed = false;
            }
        }
        ImGui::Dummy(ImVec2(0, 2.f));
    }

    bool capturing_bind() {
        return g_bind_target != nullptr;
    }

    bool poll_bind_capture() {
        if (!g_bind_target)
            return false;
        static bool was[256]{};
        if (!g_bind_armed) {
            if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) {
                g_bind_armed = true;
                for (int vk = 1; vk < 256; ++vk)
                    was[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
            }
            return true;
        }
        for (int vk = 1; vk < 256; ++vk) {
            const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
            const bool edge = down && !was[vk];
            was[vk] = down;
            if (!edge)
                continue;
            if (vk == VK_ESCAPE) {
                g_bind_target = nullptr;
                g_bind_armed = false;
                return true;
            }
            if (vk == VK_LBUTTON || vk == VK_RBUTTON)
                continue;
            *g_bind_target = vk;
            g_bind_target = nullptr;
            g_bind_armed = false;
            return true;
        }
        return true;
    }

    static const char* vk_label(int vk) {
        switch (vk) {
        case 0: return "Aucune";
        case VK_INSERT: return "INSERT";
        case VK_DELETE: return "DELETE";
        case VK_HOME: return "HOME";
        case VK_END: return "END";
        case VK_PRIOR: return "PAGE UP";
        case VK_NEXT: return "PAGE DOWN";
        case VK_SPACE: return "ESPACE";
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "SHIFT";
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "CTRL";
        case VK_MENU: case VK_LMENU: case VK_RMENU: return "ALT";
        case VK_TAB: return "TAB";
        case VK_CAPITAL: return "CAPS";
        case VK_MBUTTON: return "Souris 3";
        case VK_XBUTTON1: return "Souris 4";
        case VK_XBUTTON2: return "Souris 5";
        case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3";
        case VK_F4: return "F4"; case VK_F5: return "F5"; case VK_F6: return "F6";
        case VK_F7: return "F7"; case VK_F8: return "F8"; case VK_F9: return "F9";
        case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
        default: break;
        }
        static char buf[24];
        if (vk >= 'A' && vk <= 'Z') {
            buf[0] = static_cast<char>(vk);
            buf[1] = 0;
            return buf;
        }
        if (vk >= '0' && vk <= '9') {
            buf[0] = static_cast<char>(vk);
            buf[1] = 0;
            return buf;
        }
        std::snprintf(buf, sizeof(buf), "VK %d", vk);
        return buf;
    }

    static void draw_skins_tab() {
        section("SKINS");
        if (toggle_row("Skin changer", &cfg::skins::enabled))
            skins::force_refresh();
        if (!cfg::skins::enabled) {
            if (font_small) ImGui::PushFont(font_small);
            ImGui::TextColored(col::muted, "Visible par toi seulement");
            ImGui::TextColored(col::muted, "Uniquement skins du modele CS2");
            if (font_small) ImGui::PopFont();
        } else {
            if (font_small) ImGui::PushFont(font_small);
            ImGui::TextColored(col::muted, "Visible par toi seulement");
            ImGui::TextColored(col::muted, "Uniquement skins du modele CS2");
            ImGui::TextColored(col::muted, "En main  %s", skins::current_weapon_name());
            ImGui::TextColored(col::muted, "%s", skins::debug_line());
            if (font_small) ImGui::PopFont();

            int ncat = 0;
            const auto* cat = skins::catalog(&ncat);
            if (ncat > 0) {
                if (cfg::skins::ui_weapon < 0 || cfg::skins::ui_weapon >= ncat)
                    cfg::skins::ui_weapon = 0;

                if (ImGui::Button("Arme en main", ImVec2(-1, 0))) {
                    const auto pawn = game::local_pawn();
                    if (mem::valid(pawn)) {
                        const auto wpn = game::active_weapon(pawn);
                        if (mem::valid(wpn)) {
                            const int idx = skins::weapon_index(spread::item_index(wpn));
                            if (idx >= 0) cfg::skins::ui_weapon = idx;
                        }
                    }
                }

                ImGui::PushItemWidth(-1);
                if (ImGui::BeginCombo("##skinwpn", cat[cfg::skins::ui_weapon].name)) {
                    for (int i = 0; i < ncat; ++i) {
                        const bool sel = (cfg::skins::ui_weapon == i);
                        if (ImGui::Selectable(cat[i].name, sel))
                            cfg::skins::ui_weapon = i;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                const auto& wsel = cat[cfg::skins::ui_weapon];
                int cur_opt = 0;
                for (int i = 0; i < wsel.option_count; ++i) {
                    if (wsel.options[i].paint == cfg::skins::paint[cfg::skins::ui_weapon]) {
                        cur_opt = i;
                        break;
                    }
                }
                if (ImGui::BeginCombo("##skinkit", wsel.options[cur_opt].name)) {
                    for (int i = 0; i < wsel.option_count; ++i) {
                        const bool sel = (cur_opt == i);
                        if (ImGui::Selectable(wsel.options[i].name, sel)) {
                            cfg::skins::paint[cfg::skins::ui_weapon] = wsel.options[i].paint;
                            skins::force_refresh();
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();

                ImGui::PushItemWidth(-1);
                if (font_small) ImGui::PushFont(font_small);
                const char* wear_n = "FN";
                if (cfg::skins::wear >= 0.45f) wear_n = "BS";
                else if (cfg::skins::wear >= 0.38f) wear_n = "WW";
                else if (cfg::skins::wear >= 0.15f) wear_n = "FT";
                else if (cfg::skins::wear >= 0.07f) wear_n = "MW";
                ImGui::TextColored(col::muted, "Usure  %.3f  %s", cfg::skins::wear, wear_n);
                if (font_small) ImGui::PopFont();
                ImGui::SliderFloat("##skinwear", &cfg::skins::wear, 0.001f, 0.99f, "");
                if (font_small) ImGui::PushFont(font_small);
                ImGui::TextColored(col::muted, "Seed  %d", cfg::skins::seed);
                if (font_small) ImGui::PopFont();
                ImGui::SliderInt("##skinseed", &cfg::skins::seed, 0, 1000, "");
                ImGui::PopItemWidth();
            }
        }

        section("COUTEAU");
        if (toggle_row("Knife changer", &cfg::skins::knife_enabled))
            skins::force_refresh();
        if (cfg::skins::knife_enabled) {
            if (font_small) ImGui::PushFont(font_small);
            ImGui::TextColored(col::muted, "Visible par toi seulement");
            ImGui::TextColored(col::muted, "%s", skins::knife_debug_line());
            if (font_small) ImGui::PopFont();

            int nkn = 0;
            const auto* kcat = skins::knife_catalog(&nkn);
            if (nkn > 0) {
                if (cfg::skins::knife_index < 0 || cfg::skins::knife_index >= nkn)
                    cfg::skins::knife_index = 4;
                ImGui::PushItemWidth(-1);
                if (ImGui::BeginCombo("##knifemodel", kcat[cfg::skins::knife_index].name)) {
                    for (int i = 0; i < nkn; ++i) {
                        const bool sel = (cfg::skins::knife_index == i);
                        if (ImGui::Selectable(kcat[i].name, sel)) {
                            cfg::skins::knife_index = i;
                            skins::force_refresh();
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
            }
        }
    }

    static void nav_item(const char* label, int id) {
        const bool on = cfg::ui_tab == id;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = 38.f;
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
        const ImGuiID wid = window->GetID(label);
        ImGui::ItemSize(bb);
        ImGui::Dummy(ImVec2(0, 3.f));
        if (!ImGui::ItemAdd(bb, wid))
            return;
        bool hovered = false, held = false;
        if (ImGui::ButtonBehavior(bb, wid, &hovered, &held))
            cfg::ui_tab = id;
        ImDrawList* dl = window->DrawList;
        if (on || hovered)
            dl->AddRectFilled(bb.Min, bb.Max, u32(on ? col::card_h : col::card), 8.f);
        if (on) {
            dl->AddRectFilled(
                ImVec2(bb.Min.x + 1.f, bb.Min.y + 9.f),
                ImVec2(bb.Min.x + 4.f, bb.Max.y - 9.f),
                u32(col::white), 2.f);
        }
        const ImVec2 tsz = ImGui::CalcTextSize(label);
        dl->AddText(
            ImVec2(bb.Min.x + 16.f, bb.Min.y + (h - tsz.y) * 0.5f),
            u32(on ? col::white : col::muted),
            label);
    }

    static void page_header(const char* title, const char* sub) {
        const float y0 = ImGui::GetCursorPosY();
        const float x0 = ImGui::GetCursorPosX();
        if (font_hero) ImGui::PushFont(font_hero);
        ImGui::TextUnformatted(title);
        if (font_hero) ImGui::PopFont();
        if (font_small) ImGui::PushFont(font_small);
        ImGui::TextColored(col::muted, "%s", sub);
        if (font_small) ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 44.f, y0));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::card_h);
        ImGui::PushStyleColor(ImGuiCol_Text, col::muted);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
        if (ImGui::Button("x##close", ImVec2(28.f, 28.f)))
            cfg::menu_open = false;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::SetCursorPos(ImVec2(x0, y0 + 58.f));
        ImGui::Dummy(ImVec2(0, 4.f));
    }

    void render() {
        if (!cfg::menu_open) return;

        static bool themed = false;
        if (!themed) {
            apply_theme();
            themed = true;
        }

        static bool sized = false;
        if (!sized) {
            ImGui::SetNextWindowSize(ImVec2(720, 540), ImGuiCond_Always);
            sized = true;
        } else {
            ImGui::SetNextWindowSize(ImVec2(720, 540), ImGuiCond_FirstUseEver);
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(640, 460), ImVec2(920, 740));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        ImGui::Begin(
            "##cspro_overlay",
            &cfg::menu_open,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

        const float side_w = 196.f;
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        wdl->AddRectFilled(wp, ImVec2(wp.x + side_w, wp.y + ws.y), u32(col::side), 16.f, ImDrawFlags_RoundCornersLeft);
        wdl->AddLine(
            ImVec2(wp.x + side_w, wp.y + 14.f),
            ImVec2(wp.x + side_w, wp.y + ws.y - 14.f),
            u32(col::line), 1.f);

        ImGui::BeginChild("nav", ImVec2(side_w, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        ImGui::SetCursorPos(ImVec2(20.f, 22.f));
        if (font_title) ImGui::PushFont(font_title);
        ImGui::TextUnformatted("CS Pro Tool");
        if (font_title) ImGui::PopFont();
        if (font_small) ImGui::PushFont(font_small);
        ImGui::SetCursorPosX(20.f);
        ImGui::TextColored(col::muted, "Counter-Strike 2 \xc2\xb7 " CSPT_VERSION);
        if (font_small) ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(12.f, 86.f));
        ImGui::BeginGroup();
        ImGui::PushItemWidth(side_w - 24.f);
        nav_item("Ragebot", 0);
        nav_item("Aimbot", 1);
        nav_item("ESP", 2);
        nav_item("Visuels", 3);
        nav_item("Skins", 4);
        nav_item("Systeme", 5);
        ImGui::PopItemWidth();
        ImGui::EndGroup();

        if (font_small) ImGui::PushFont(font_small);
        char foot[48]{};
        std::snprintf(foot, sizeof(foot), "Overlay  %.0f fps", ImGui::GetIO().Framerate);
        ImGui::SetCursorPos(ImVec2(20.f, ImGui::GetWindowHeight() - 58.f));
        ImGui::TextColored(col::muted, "%s", foot);
        ImGui::SetCursorPosX(20.f);
        ImGui::TextColored(col::muted, "HOME  panneau");
        ImGui::SetCursorPosX(20.f);
        ImGui::TextColored(col::muted, "v1.0");
        if (font_small) ImGui::PopFont();
        ImGui::EndChild();

        ImGui::SameLine(0, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.f, 20.f));
        ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

        if (cfg::ui_tab == 0)
            page_header("Ragebot", "Tir silent ecran + autoshoot. Bhop separe. AA plus tard.");
        else if (cfg::ui_tab == 1)
            page_header("Aimbot", "Silent, trigger, focus et FOV");
        else if (cfg::ui_tab == 2)
            page_header("ESP", "Boxes, squelettes et affichage des joueurs");
        else if (cfg::ui_tab == 3)
            page_header("Visuels", "Couleurs, camera et FOV monde");
        else if (cfg::ui_tab == 4)
            page_header("Skins", "Skins et couteau (toi seulement)");
        else
            page_header("Systeme", "Presets, connexion et fermeture");

        if (cfg::ui_tab == 0) {
            section("RAGEBOT (TIR)");
            if (ImGui::Button("Activer Ragebot + Auto-Bhop", ImVec2(-1.f, 36.f)))
                features::apply_rage_preset();
            if (font_small) ImGui::PushFont(font_small);
            ImGui::TextColored(col::muted, "HC live: %.0f%%", features::ragebot_last_hc());
            ImGui::TextColored(col::muted,
                "Silent a l'ecran seulement (pas de 360). AA / resolver = plus tard.");
            if (font_small) ImGui::PopFont();
            toggle_row("Ragebot ON", &cfg::combat::ragebot);
            toggle_row("Silent Aim (ecran)", &cfg::combat::silent);
            toggle_row("Viser le corps", &cfg::combat::aim_body);
            toggle_row("Autowall", &cfg::combat::autowall);
            toggle_row("Check murs", &cfg::combat::wall_check);
            toggle_row("Team check", &cfg::combat::team_check);

            section("AUTOSHOOT / SSG");
            toggle_row("Autoshoot", &cfg::combat::ragebot_autoshoot);
            toggle_row("Auto-Stop (micro counter-strafe)", &cfg::combat::ragebot_autostop);
            toggle_row("Air Apex (tir au sommet)", &cfg::combat::ragebot_air_apex);
            tiny_slider("Hitchance sol %", &cfg::combat::ragebot_hitchance, 1.f, 100.f);
            tiny_slider("Hitchance air %", &cfg::combat::ragebot_hitchance_air, 1.f, 100.f);
            toggle_row("Multi-Tap (pas SSG)", &cfg::combat::ragebot_multitap);
            if (cfg::combat::ragebot_multitap) {
                float taps = static_cast<float>(cfg::combat::ragebot_multitap_count);
                tiny_slider("Balles / burst", &taps, 1.f, 4.f);
                cfg::combat::ragebot_multitap_count = static_cast<int>(taps + 0.5f);
            }
            if (font_small) ImGui::PushFont(font_small);
            ImGui::TextColored(col::muted, "SSG: vise libre + bhop. Stop 1-2 ticks a l'apex puis tir.");
            ImGui::TextColored(col::muted, "Cam jamais figee (silent clear apres chaque tir).");
            ImGui::TextColored(col::muted, "Hitchance = taille tete vs cone dispersion.");
            if (font_small) ImGui::PopFont();

            section("MOVEMENT (BHOP)");
            toggle_row("Auto-Bunnyhop", &cfg::combat::bhop);
            toggle_row("Airstrafe", &cfg::combat::airstrafe);
            toggle_row("Fast stop", &cfg::combat::fast_stop);
            if (font_small) ImGui::PushFont(font_small);
            ImGui::TextColored(col::muted, "Bhop = transport. Ragebot = tir (autostop a l'apex).");
            ImGui::TextColored(col::muted, "Espace reste appuye pour le bunnyhop.");
            if (font_small) ImGui::PopFont();
        }
        else if (cfg::ui_tab == 1) {
            section("VISEE");
            toggle_row("Aimbot (cercle)", &cfg::combat::aimbot);
            toggle_row("Rage (tout ecran)", &cfg::combat::rage);
            toggle_row("Silent (a l'ecran)", &cfg::combat::silent);
            toggle_row("Silent 360", &cfg::combat::silent_360);
            toggle_row("Viser le corps", &cfg::combat::aim_body);
            toggle_row("Triggerbot", &cfg::combat::triggerbot);
            if (cfg::combat::triggerbot)
                tiny_slider("FOV trigger", &cfg::combat::trigger_fov, 2.f, 20.f);
            {
                const char* prio[] = { "Centre", "HP bas", "Plus proche" };
                if (cfg::combat::aim_priority < 0 || cfg::combat::aim_priority > 2)
                    cfg::combat::aim_priority = 0;
                if (font_small) ImGui::PushFont(font_small);
                ImGui::TextColored(col::muted, "Priorite cible");
                if (font_small) ImGui::PopFont();
                ImGui::PushItemWidth(-1);
                ImGui::Combo("##aimprio", &cfg::combat::aim_priority, prio, 3);
                ImGui::PopItemWidth();
            }
            {
                if (font_small) ImGui::PushFont(font_small);
                ImGui::TextColored(col::muted, "Focus joueur");
                if (font_small) ImGui::PopFont();
                const auto roster = game::world_ready()
                    ? game::collect_players(true)
                    : std::vector<game::Player>{};
                char preview[80]{};
                if (!cfg::combat::focus_id)
                    std::snprintf(preview, sizeof(preview), "Aucun (auto)");
                else if (cfg::combat::focus_name[0])
                    std::snprintf(preview, sizeof(preview), "%s", cfg::combat::focus_name);
                else
                    std::snprintf(preview, sizeof(preview), "Joueur verrouille");
                bool still_in = false;
                ImGui::PushItemWidth(-1);
                if (ImGui::BeginCombo("##focusply", preview)) {
                    if (ImGui::Selectable("Aucun (auto)", cfg::combat::focus_id == 0)) {
                        cfg::combat::focus_id = 0;
                        cfg::combat::focus_name[0] = 0;
                    }
                    for (const auto& p : roster) {
                        const uint64_t id = game::player_id(p);
                        if (id == cfg::combat::focus_id) still_in = true;
                        char row[96]{};
                        std::snprintf(row, sizeof(row), "%s  %s  %dhp",
                            p.teammate ? "AMI" : "ENN",
                            p.name.empty() ? "?" : p.name.c_str(),
                            p.health);
                        const bool sel = (id == cfg::combat::focus_id);
                        if (ImGui::Selectable(row, sel)) {
                            cfg::combat::focus_id = id;
                            std::snprintf(cfg::combat::focus_name, sizeof(cfg::combat::focus_name),
                                "%s", p.name.empty() ? "?" : p.name.c_str());
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                if (cfg::combat::focus_id && !still_in) {
                    for (const auto& p : roster) {
                        if (game::player_id(p) == cfg::combat::focus_id) {
                            still_in = true;
                            break;
                        }
                    }
                }
                if (font_small) ImGui::PushFont(font_small);
                if (cfg::combat::focus_id)
                    ImGui::TextColored(col::muted, "Lock: %s (hors FOV OK)",
                        cfg::combat::focus_name[0] ? cfg::combat::focus_name : "...");
                else
                    ImGui::TextColored(col::muted, "Pas de lock : priorite auto");
                if (font_small) ImGui::PopFont();
            }
            toggle_row("Spinbot", &cfg::combat::spinbot);
            if (cfg::combat::spinbot)
                tiny_slider("Vitesse spin (deg/s)", &cfg::combat::spinbot_speed, 20.f, 360.f);
            toggle_row("Check murs", &cfg::combat::wall_check);
            toggle_row("Autowall", &cfg::combat::autowall);
            toggle_row("Team check", &cfg::combat::team_check);

            section("MOUVEMENT");
            toggle_row("Bunnyhop", &cfg::combat::bhop);
            toggle_row("Airstrafe", &cfg::combat::airstrafe);
            toggle_row("Fast stop", &cfg::combat::fast_stop);
            toggle_row("SSG jump shot", &cfg::combat::ssg_jump);
            ImGui::TextColored(col::muted, "Jumpscout : espace, tir si inacc basse. OK avec silent/360.");

            section("FOV");
            toggle_row("Afficher cercle", &cfg::visuals::fov_circle);
            tiny_slider("Rayon FOV", &cfg::combat::fov, 20.f, 400.f);

            if (font_small) ImGui::PushFont(font_small);
            ImGui::TextColored(col::muted, "Corps dans le cercle = lock tete instant");
            ImGui::TextColored(col::muted, "Silent : tue la tete a l'ecran, souris libre.");
            ImGui::TextColored(col::muted, "Silent 360 : aussi derriere / hors ecran.");
            ImGui::TextColored(col::muted, "Corps : vise le torse, pas le cou.");
            ImGui::TextColored(col::muted, "Spinbot : toi tu vises. Les autres voient 1 perso qui tourne.");
            ImGui::TextColored(col::muted, "3e pers. : pas de fantome. Deg/s (90 = 1 tour / 4s).");
            ImGui::TextColored(col::muted, "Focus : un joueur de la liste, ignore le FOV.");
            ImGui::TextColored(col::muted, "Triggerbot : tire si ennemi sous le visuel.");
            ImGui::TextColored(col::muted, "Check murs ON = pas de lock a travers");
            ImGui::TextColored(col::muted, "Autowall : wallbang si AWP/SSG/AK (vis d'abord)");
            ImGui::TextColored(col::muted, "Team check OFF = deathmatch (tout le monde)");
            ImGui::TextColored(col::muted, "Bhop : espace reste appuye, hop a chaque atterrissage");
            ImGui::TextColored(col::muted, "Airstrafe : angle ideal (W+souris). D/S/A = tes touches");
            ImGui::TextColored(col::muted, "Fast stop : arret quand tu laches ZQSD (pas en bhop)");
            ImGui::TextColored(col::muted, "SSG jump : stop seulement si cible + course rapide");
            ImGui::TextColored(col::muted, "HOME ferme le menu (visee active)");
            ImGui::TextColored(
                hooks::createmove_active() ? col::text : col::muted,
                hooks::createmove_active()
                    ? "CreateMove OK (vtable CCSGOInput)"
                    : "CreateMove attente...");
            if (font_small) ImGui::PopFont();
        }
        else if (cfg::ui_tab == 2) {
            section("AFFICHAGE");
            toggle_row("ESP", &cfg::visuals::esp);
            toggle_row("Team check", &cfg::combat::team_check);
            if (cfg::combat::team_check)
                toggle_row("Allies", &cfg::visuals::show_teammates);
            toggle_row("Squelettes", &cfg::visuals::skeleton);
            toggle_row("Chams 3D (plein)", &cfg::visuals::glow);
            if (cfg::visuals::glow)
                color_row("Couleur glow", cfg::visuals::glow_color);
            toggle_row("Items sol", &cfg::visuals::item_glow);
            toggle_row("Noms", &cfg::visuals::names);
            toggle_row("Photo Steam", &cfg::visuals::avatars);
            toggle_row("HP", &cfg::visuals::hp_text);
            toggle_row("Arme", &cfg::visuals::weapon);
            toggle_row("Bombe", &cfg::visuals::bomb_timer);
            toggle_row("Snaplines", &cfg::visuals::snaplines);
            toggle_row("Fleches hors ecran", &cfg::visuals::offscreen);
            toggle_row("Rainbow", &cfg::visuals::rainbow);
            toggle_row("Liste spectateurs", &cfg::visuals::spec_list);

            section("TRACERS");
            toggle_row("Balles tracantes", &cfg::visuals::tracers);
            if (cfg::visuals::tracers) {
                toggle_row("Mes balles", &cfg::visuals::tracers_local);
                toggle_row("Ennemis", &cfg::visuals::tracers_enemy);
                toggle_row("Allies", &cfg::visuals::tracers_ally);
                tiny_slider("Duree (s)", &cfg::visuals::tracers_time, 0.4f, 4.f);
                color_row("Moi", cfg::visuals::tracers_color_local);
                color_row("Allie", cfg::visuals::tracers_color_ally);
                color_row("Ennemi", cfg::visuals::tracers_color_enemy);
                if (font_small) ImGui::PushFont(font_small);
                ImGui::TextColored(col::muted, "Suit la vraie trajectoire (spread + course)");
                if (font_small) ImGui::PopFont();
            }

            section("COULEUR");
            color_row("Couleur ESP", cfg::visuals::esp_color);
        }
        else if (cfg::ui_tab == 3) {
            section("CAMERA");
            toggle_row("3eme personne", &cfg::visuals::third_person);
            if (cfg::visuals::third_person)
                tiny_slider("Distance", &cfg::visuals::third_person_dist, 40.f, 250.f);

            section("FOV JOUEUR");
            toggle_row("FOV joueur", &cfg::visuals::player_fov);
            if (cfg::visuals::player_fov)
                tiny_slider("FOV", &cfg::visuals::player_fov_value, 60.f, 160.f);

            section("SCOPE");
            toggle_row("Pas d'overlay AWP/SSG", &cfg::visuals::no_scope);
            toggle_row("No flash", &cfg::visuals::no_flash);
            toggle_row("Hitmarker", &cfg::visuals::hitmarker);
            ImGui::TextColored(col::muted, "Blanc = hit, rouge = kill, croix + degats");
            ImGui::TextColored(col::muted, "Flash : visible par toi seulement");

            section("ATMOSPHERE");
            toggle_row("Ciel custom", &cfg::visuals::atmosphere);
            if (cfg::visuals::atmosphere) {
                color_row("Couleur ciel", cfg::visuals::atmosphere_color);
                tiny_slider("Intensite ciel", &cfg::visuals::atmosphere_intensity, 20.f, 400.f);
                tiny_slider("Nuit", &cfg::visuals::atmosphere_night, 0.f, 100.f);
                if (font_small) ImGui::PushFont(font_small);
                ImGui::TextColored(col::muted, "Ciel %d  update %s",
                    tools::atm_ents.load(),
                    tools::atm_upd.load() ? "OK" : "non");
                if (font_small) ImGui::PopFont();
            }

            section("RACCOURCIS");
            bind_row("ESP", &cfg::visuals::esp_key);
            bind_row("Silent", &cfg::combat::silent_key);
            bind_row("3e personne", &cfg::visuals::third_person_key);
            if (font_small) ImGui::PushFont(font_small);
            ImGui::TextColored(col::muted, "HOME  menu");
            ImGui::TextColored(col::muted, "END  unload");
            ImGui::TextColored(col::muted, "Clic droit sur un bind = aucune");
            ImGui::TextColored(col::muted, "Echap annule la capture");
            if (font_small) ImGui::PopFont();

            section("OUTILS");
            if (font_small) ImGui::PushFont(font_small);
            status_row("client.dll", tools::client);
            status_row("engine2.dll", tools::engine);
            status_row("scenesystem", tools::scene);
            status_row("schema", tools::schema);
            status_row("Present", tools::present);
            status_row("Menu", tools::imgui);
            status_row("Monde", tools::world);
            status_row("Silent hook", tools::silent_hook);
            status_row("Precision", tools::precision);
            ImGui::TextColored(col::muted, "CM ticks  %u", hooks::createmove_ticks());
            ImGui::TextColored(col::muted, "Cibles    %d", tools::last_targets.load());
            if (font_small) ImGui::PopFont();
        }
        else if (cfg::ui_tab == 4) {
            draw_skins_tab();
        }
        else {
            static char preset_name[64]{};
            static int preset_refresh = 0;

            section("NOUVEAU PRESET");
            ImGui::PushItemWidth(-1);
            ImGui::InputTextWithHint("##presetname", "Nom du preset...", preset_name, sizeof(preset_name));
            ImGui::PopItemWidth();
            ImGui::Dummy(ImVec2(0, 2));
            if (ImGui::Button("Sauvegarder les reglages", ImVec2(-1, 0))) {
                if (presets::save(preset_name))
                    preset_refresh++;
            }

            section("MES PRESETS");
            if (preset_refresh >= 0) {
                const auto names = presets::list();
                if (names.empty()) {
                    if (font_small) ImGui::PushFont(font_small);
                    ImGui::TextColored(col::muted, "Aucun preset. Regle le cheat puis sauvegarde.");
                    if (font_small) ImGui::PopFont();
                } else {
                    for (const auto& n : names) {
                        const float row_w = ImGui::GetContentRegionAvail().x;
                        const float del_w = 28.f;
                        const float btn_w = row_w - del_w - 4.f;

                        ImGui::PushID(n.c_str());
                        if (ImGui::Button(n.c_str(), ImVec2(btn_w, 0)))
                            presets::load(n.c_str());
                        ImGui::SameLine(0, 4);
                        ImGui::PushStyleColor(ImGuiCol_Button, col::rgba(80, 30, 30));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::rgba(120, 40, 40));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, col::rgba(150, 50, 50));
                        if (ImGui::Button("X", ImVec2(del_w, 0))) {
                            presets::remove(n.c_str());
                            preset_refresh++;
                        }
                        ImGui::PopStyleColor(3);
                        ImGui::PopID();
                    }
                }
            }

            if (presets::last_msg[0]) {
                ImGui::Dummy(ImVec2(0, 4));
                if (font_small) ImGui::PushFont(font_small);
                ImGui::TextColored(col::text, "%s", presets::last_msg);
                if (font_small) ImGui::PopFont();
            }

            if (font_small) ImGui::PushFont(font_small);
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::TextColored(col::muted, "Clique un nom pour appliquer le preset.");
            ImGui::TextColored(col::muted, "Sauvegarde = reglages actuels du menu.");
            if (font_small) ImGui::PopFont();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
}
