#include "hooks/present.hpp"
#include "hooks/chamsdraw.hpp"
#include "common.hpp"
#include "menu/menu.hpp"
#include "features/esp.hpp"
#include "features/aimbot.hpp"
#include "features/bhop.hpp"
#include "features/ssg.hpp"
#include "features/hitmarker.hpp"
#include "features/tracers.hpp"
#include "features/misc.hpp"
#include "hooks/createmove.hpp"
#include "hooks/nospread.hpp"
#include "core/modules.hpp"
#include "core/status.hpp"
#include "sdk/entity.hpp"
#include "features/silent.hpp"
#include "features/skins.hpp"
#include "features/avatars.hpp"

#include "kiero.h"
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <memoryapi.h>
#include <fstream>

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace hooks {
    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    PresentFn o_present = nullptr;
    WNDPROC o_wndproc = nullptr;

    HWND game_window = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    bool imgui_ready = false;
    bool logged_first_present = false;
    int exception_logs = 0;
    static int feature_cooldown = 0;

    static void log_line(const char* msg) {
        std::ofstream f("C:\\Users\\Hugo\\Desktop\\csprotool_log.txt", std::ios::app);
        if (!f) return;
        f << msg << "\n";
    }

    static void allow_cfg_target(void* fn) {
        if (!fn) return;
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(fn, &mbi, sizeof(mbi))) return;
        using Fn = BOOL(WINAPI*)(HANDLE, PVOID, SIZE_T, ULONG, CFG_CALL_TARGET_INFO*);
        const auto pSet = reinterpret_cast<Fn>(
            GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "SetProcessValidCallTargets"));
        if (!pSet) return;
        CFG_CALL_TARGET_INFO info{};
        info.Offset = static_cast<ULONG_PTR>(reinterpret_cast<uint8_t*>(fn) - static_cast<uint8_t*>(mbi.BaseAddress));
        info.Flags = CFG_CALL_TARGET_VALID;
        pSet(GetCurrentProcess(), mbi.BaseAddress, mbi.RegionSize, 1, &info);
    }

    static void handle_toggle_keys() {
        static bool home_was_down = false;
        const bool home_down = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
        if (home_down && !home_was_down)
            cfg::menu_open = !cfg::menu_open;
        home_was_down = home_down;
    }

    LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (cfg::menu_open && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
            return true;
        return CallWindowProcW(o_wndproc, hwnd, msg, wp, lp);
    }

    static bool ensure_device(IDXGISwapChain* swap) {
        if (device && context) return true;
        ID3D11Device* dev = nullptr;
        if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev))) || !dev)
            return false;
        device = dev;
        device->GetImmediateContext(&context);
        return context != nullptr;
    }

    static bool init_imgui(IDXGISwapChain* swap) {
        if (!ensure_device(swap)) {
            log_line("[!] GetDevice failed");
            return false;
        }

        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swap->GetDesc(&desc)) || !desc.OutputWindow) {
            log_line("[!] GetDesc failed");
            return false;
        }
        game_window = desc.OutputWindow;

        if (!o_wndproc) {
            o_wndproc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wnd_proc)));
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.MouseDrawCursor = false;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::StyleColorsDark();
        menu::setup_fonts();

        if (!ImGui_ImplWin32_Init(game_window) || !ImGui_ImplDX11_Init(device, context)) {
            log_line("[!] ImGui backend init failed");
            return false;
        }

        cfg::menu_open = true;
        imgui_ready = true;
        tools::set(tools::imgui, tools::St::Ok);
        log_line("[+] imgui ready");
        return true;
    }

    static void draw_ui_only() {
        handle_toggle_keys();

        ImGuiIO& io = ImGui::GetIO();
        if (cfg::menu_open) {
            io.MouseDrawCursor = true;
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
            POINT p{};
            if (game_window && GetCursorPos(&p) && ScreenToClient(game_window, &p))
                io.AddMousePosEvent(static_cast<float>(p.x), static_cast<float>(p.y));
            io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
            io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
        } else {
            io.MouseDrawCursor = false;
            io.WantCaptureMouse = false;
            io.WantCaptureKeyboard = false;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        }

        menu::render();
        auto* dl = ImGui::GetBackgroundDrawList();
        // status drawn by ESP overlay
    }

    // Caches combat (mort / fin de manche): on oublie les pointeurs sans ecrire.
    static void drop_combat_caches() {
        __try { features::shutdown_esp(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { features::reset_hitmarker(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { features::reset_tracers(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        __try { skins::on_world_lost(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // A la sortie de partie le moteur libere toutes les entites: on lache les
    // pointeurs mis en cache avant qu'ils ne soient reutilises par le jeu.
    static void drop_world_caches() {
        __try { features::on_world_lost(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        drop_combat_caches();
    }

    static void draw_features(int w, int h) {
        static bool in_world = false;
        static bool was_alive = false;
        static bool death_caches_dropped = false;
        static bool logged_world_lost = false;
        static int ready_streak = 0;
        static int lost_streak = 0;

        if (!modules::ready() || w <= 0 || h <= 0)
            return;

        // world_ready() clignote a la mort / fin de manche: debounce ~200 ms.
        const bool ready_now = game::world_ready();
        const bool session_gone = !game::session_connected();

        if (session_gone) {
            ready_streak = 0;
            lost_streak = 12;
        } else if (ready_now) {
            lost_streak = 0;
            if (++ready_streak >= 2)
                in_world = true;
        } else if (in_world) {
            ready_streak = 0;
            if (++lost_streak >= 12)
                in_world = false;
        } else {
            ready_streak = 0;
        }

        if (!in_world) {
            if ((lost_streak >= 12 || session_gone) && !logged_world_lost) {
                was_alive = false;
                death_caches_dropped = false;
                drop_world_caches();
                log_line("[*] sortie de partie — caches liberes");
                logged_world_lost = true;
            }
            tools::set(tools::world, tools::St::Wait);
            auto* dl = ImGui::GetBackgroundDrawList();
            dl->AddText(ImVec2(16, 16), IM_COL32(245, 245, 245, 220),
                "CS Pro Tool | attente monde (entre en partie)");
            return;
        }
        logged_world_lost = false;

        const bool alive = game::local_alive();
        if (alive) {
            death_caches_dropped = false;
        } else if (!death_caches_dropped && was_alive) {
            drop_combat_caches();
            death_caches_dropped = true;
            log_line("[*] mort — caches combat liberes");
        }
        was_alive = alive;

        tools::set(tools::world, tools::St::Ok);
        // hide_camera seulement si shooting (voir silent.hpp) — jamais en continue.

        if (alive) {
            features::run_aimbot(w, h);
            features::run_bhop();
            features::run_ssg();
            features::run_visual_removals();
            if (!skins::hooks_ready())
                skins::run(false);
            features::run_hitmarker(w, h);
            features::run_tracers(w, h);
        }

        features::run_fov_changer();
        features::run_third_person();
        features::run_atmosphere();
        avatars::tick();
        features::render_esp(w, h);
        features::render_ssg_hud(w, h);
    }

    static void render_overlay(IDXGISwapChain* swap) {
        if (!logged_first_present) {
            logged_first_present = true;
            log_line("[+] Present hooked & called");
        }

        if (!imgui_ready && !init_imgui(swap))
            return;

        if (!ensure_device(swap) || !context)
            return;

        ID3D11Texture2D* back = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back)
            return;
        if (FAILED(device->CreateRenderTargetView(back, nullptr, &rtv)) || !rtv) {
            back->Release();
            return;
        }
        back->Release();

        // Backup a few states
        ID3D11RenderTargetView* old_rtv = nullptr;
        ID3D11DepthStencilView* old_dsv = nullptr;
        context->OMGetRenderTargets(1, &old_rtv, &old_dsv);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        draw_ui_only();

        int w = 0, h = 0;
        if (game_window) {
            RECT rc{};
            GetClientRect(game_window, &rc);
            w = rc.right - rc.left;
            h = rc.bottom - rc.top;
        }

        __try {
            if (feature_cooldown > 0)
                --feature_cooldown;
            else
                draw_features(w, h);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            feature_cooldown = 180;
            if (exception_logs < 8) {
                log_line("[!] exception in features — pause 3s");
                ++exception_logs;
            }
        }

        ImGui::Render();
        context->OMSetRenderTargets(1, &rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        context->OMSetRenderTargets(1, &old_rtv, old_dsv);
        if (old_rtv) old_rtv->Release();
        if (old_dsv) old_dsv->Release();
        rtv->Release();
    }

    HRESULT __stdcall hk_present(IDXGISwapChain* swap, UINT sync, UINT flags) {
        __try {
            render_overlay(swap);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (exception_logs < 20) {
                char buf[96];
                wsprintfA(buf, "[!] exception in present code=0x%08X", GetExceptionCode());
                log_line(buf);
                ++exception_logs;
            }
        }
        return o_present(swap, sync, flags);
    }

    ID3D11Device* d3d_device() { return device; }
    ID3D11DeviceContext* d3d_context() { return context; }

    bool init() {
        log_line("[*] kiero::init...");
        const auto st = kiero::init(kiero::RenderType::D3D11);
        if (st != kiero::Status::Success && st != kiero::Status::AlreadyInitializedError) {
            log_line("[!] kiero::init failed");
            return false;
        }
        if (o_present)
            return true;
        if (kiero::bind(8, reinterpret_cast<void**>(&o_present), reinterpret_cast<void*>(&hk_present)) != kiero::Status::Success) {
            log_line("[!] bind Present failed");
            return false;
        }
        allow_cfg_target(reinterpret_cast<void*>(&hk_present));
        allow_cfg_target(reinterpret_cast<void*>(&wnd_proc));
        log_line("[+] Present bound (index 8)");
        return true;
    }

    void shutdown() {
        if (game_window && o_wndproc) {
            SetWindowLongPtrW(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(o_wndproc));
            o_wndproc = nullptr;
        }
        if (imgui_ready) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            imgui_ready = false;
        }
        if (context) { context->Release(); context = nullptr; }
        if (device) { device->Release(); device = nullptr; }
        shutdown_createmove();
        shutdown_nospread();
        skins::shutdown_hooks();
        features::shutdown_esp();
        features::shutdown_no_ally_clip();
        avatars::shutdown();
        features::shutdown_third_person();
        cham_draw::shutdown_hooks();
        kiero::shutdown();
    }
}



