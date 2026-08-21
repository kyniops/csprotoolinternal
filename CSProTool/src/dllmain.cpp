#include "common.hpp"
#include "hooks/present.hpp"
#include "hooks/createmove.hpp"
#include "hooks/nospread.hpp"
#include "features/skins.hpp"
#include "core/modules.hpp"
#include "core/status.hpp"
#include "core/version.hpp"
#include "sdk/entity.hpp"
#include <fstream>
#include <cstdarg>

static HMODULE g_module = nullptr;
static HANDLE g_thread = nullptr;

static void log_line(const char* msg) {
    std::ofstream f("C:\\Users\\Hugo\\Desktop\\csprotool_log.txt", std::ios::app);
    if (!f) return;
    f << msg << "\n";
}

static void logf(const char* fmt, ...) {
    char buf[256]{};
    va_list ap;
    va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap);
    va_end(ap);
    log_line(buf);
}

static bool wait_mod(const wchar_t* wname, const char* aname, std::atomic<int>& st, int timeout_ms) {
    logf("[.] attente %s", aname);
    if (!modules::wait_for(wname, &st, timeout_ms)) {
        logf("[!] %s introuvable", aname);
        return false;
    }
    logf("[+] %s OK", aname);
    return true;
}

static DWORD WINAPI bootstrap(LPVOID) {
    tools::running.store(true, std::memory_order_relaxed);
    log_line("[*] CSProTool " CSPT_VERSION " — bootstrap start");
    Beep(600, 80);

    // 1) DLLs du jeu, une par une (injection trop tot = on attend)
    constexpr int k_dll_timeout = 180000;
    wait_mod(L"client.dll", "client.dll", tools::client, k_dll_timeout);
    wait_mod(L"engine2.dll", "engine2.dll", tools::engine, k_dll_timeout);
    wait_mod(L"scenesystem.dll", "scenesystem.dll", tools::scene, k_dll_timeout);
    wait_mod(L"schemasystem.dll", "schemasystem.dll", tools::schema, 60000);

    if (!modules::ready()) {
        log_line("[!] client.dll / engine2.dll absents — abandon");
        Beep(300, 400);
        tools::running.store(false);
        return 1;
    }

    // 2) Laisse le jeu finir de charger les interfaces
    Sleep(800);

    // 3) Present / D3D11 — on retente tant que le swapchain n'existe pas
    log_line("[.] attente Present (D3D11)");
    tools::set(tools::present, tools::St::Wait);
    bool hooked = false;
    for (int i = 0; i < 240 && tools::running.load(); ++i) {
        if (hooks::init()) {
            hooked = true;
            break;
        }
        Sleep(250);
    }

    if (!hooked) {
        log_line("[!] hooks::init FAILED");
        tools::set(tools::present, tools::St::Fail);
        Beep(300, 200); Beep(300, 200);
        tools::running.store(false);
        return 2;
    }
    tools::set(tools::present, tools::St::Ok);
    log_line("[+] Present OK — attente menu + monde + silent");
    Beep(900, 120);

    // 4) Chaque outil combat s'active quand il est pret (retry, pas one-shot)
    DWORD last_cm = 0;
    bool precision_done = false;

    while (tools::running.load() && !(GetAsyncKeyState(VK_END) & 1)) {
        const bool in_world = game::world_ready();
        tools::set(tools::world, in_world ? tools::St::Ok : tools::St::Wait);

        if (!precision_done) {
            if (hooks::init_nospread()) {
                precision_done = true;
                tools::set(tools::precision, tools::St::Ok);
            }
        }

        if (!hooks::createmove_active()) {
            const DWORD now = GetTickCount();
            if (now - last_cm > 2000) {
                last_cm = now;
                tools::set(tools::silent_hook, tools::St::Wait);
                if (hooks::init_createmove())
                    tools::set(tools::silent_hook, tools::St::Ok);
            }
        } else {
            tools::set(tools::silent_hook, tools::St::Ok);
        }

        skins::init_hooks();

        Sleep(100);
    }

    log_line("[*] shutting down");
    tools::running.store(false);
    hooks::shutdown();
    return 0;
}

extern "C" __declspec(dllexport) void Start() {
    if (g_thread)
        return;
    log_line("[*] Start()");
    g_thread = CreateThread(nullptr, 0, bootstrap, nullptr, 0, nullptr);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        Start();
    } else if (reason == DLL_PROCESS_DETACH) {
        tools::running.store(false);
        hooks::shutdown();
    }
    return TRUE;
}
