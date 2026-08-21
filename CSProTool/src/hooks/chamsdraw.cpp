#include "hooks/chamsdraw.hpp"
#include "features/config.hpp"
#include "sdk/entity.hpp"
#include "core/memory.hpp"
#include "core/pattern.hpp"
#include "MinHook.h"
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <fstream>

namespace cham_draw {
    // v282: unlit flat (style image 2) — 1 passe couleur unique (xqz), pas de dual.
    // Pas de spotted. Garde reentrance + capacite buffer (crash v259).

    struct Target {
        uintptr_t ent{};
        uint8_t r{}, g{}, b{};
    };

    struct Kv3Id {
        const char* name{};
        uint64_t a{};
        uint64_t b{};
    };

    struct StrongHandle {
        struct Binding { void* data; };
        const Binding* binding{};
        void* instance() const {
            return (binding && mem::valid(reinterpret_cast<uintptr_t>(binding)))
                ? binding->data : nullptr;
        }
    };

    // CMeshPrimitiveOutputBuffer — layout des bases CS2.
    struct PrimBuf {
        uint8_t* out{};
        int max_prims{};
        int start_prim{};
    };

    static constexpr size_t k_mesh_stride = 0x68;
    // GenPrim output stride on current CS2 build (0x68 crashait au paint).
    static constexpr size_t k_gen_stride = 0x70;
    static constexpr uintptr_t k_scene = 0x18;
    static constexpr uintptr_t k_mat0 = 0x20;
    static constexpr uintptr_t k_mat1 = 0x28;
    static constexpr uintptr_t k_color = 0x50;

    static Target g_allow[64]{};
    static int g_nallow = 0;
    static std::atomic<bool> g_on{ false };

    static bool g_hooks = false;
    static bool g_tried = false;
    static void* g_hook_addr = nullptr;      // GenPrim (ou DrawObject si seul)
    static void* g_hook_draw_addr = nullptr; // DrawObject en complement GenPrim
    static bool g_use_genprim = false;
    static void** g_gen_vt_slot = nullptr; // &vtable[4] si hook vtable
    static void* g_gen_vt_orig = nullptr;

    using GenPrimFn = void(__fastcall*)(void*, void*, void*, PrimBuf*);
    static GenPrimFn o_gen = nullptr;

    using DrawObjectFn = void(__fastcall*)(void*, void*, uint8_t*, int, void*, void*, void*, void*);
    static DrawObjectFn o_draw = nullptr;

    static void* g_mat_vis = nullptr;
    static void* g_mat_xqz = nullptr;
    static bool g_mats_ok = false;

    static std::atomic<uint32_t> g_stat_calls{ 0 };
    static std::atomic<uint32_t> g_stat_owner{ 0 };
    static std::atomic<uint32_t> g_stat_match{ 0 };
    static std::atomic<uint32_t> g_logged_first{ 0 };
    static DWORD g_last_stat_ms = 0;
    static thread_local int g_gen_depth = 0;

    static void log_line(const char* m) {
        std::ofstream f("C:\\Users\\Hugo\\Desktop\\csprotool_log.txt", std::ios::app);
        if (f) f << m << "\n";
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

    static bool find_target(uintptr_t ent, uint8_t& r, uint8_t& g, uint8_t& b) {
        if (!ent || !g_on.load(std::memory_order_relaxed))
            return false;
        for (int i = 0; i < g_nallow; ++i) {
            if (g_allow[i].ent != ent)
                continue;
            r = g_allow[i].r;
            g = g_allow[i].g;
            b = g_allow[i].b;
            return true;
        }
        return false;
    }

    static uintptr_t owner_from_scene(uintptr_t scene) {
        if (!mem::valid(scene))
            return 0;
        static const uint32_t k_offs[] = {
            0xB8, 0xC0, 0xC8, 0xD0, 0xD8, 0xE0, 0xE8, 0xF0, 0xF8, 0x110
        };
        for (const auto off : k_offs) {
            const uint32_t h = mem::read<uint32_t>(scene + off);
            if (!h || h == 0xFFFFFFFFu)
                continue;
            const auto ent = game::get_entity(h);
            if (!mem::valid(ent))
                continue;
            uint8_t r, g, b;
            if (find_target(ent, r, g, b))
                return ent;
        }
        return 0;
    }

    static uintptr_t owner_from_mesh(uint8_t* mesh) {
        if (!mesh || !mem::valid(reinterpret_cast<uintptr_t>(mesh)))
            return 0;
        const auto scene = *reinterpret_cast<uintptr_t*>(mesh + k_scene);
        return owner_from_scene(scene);
    }

    static void paint_meshes(uint8_t* mesh, int count, void* mat, uint8_t r, uint8_t g, uint8_t b) {
        if (!mesh || count <= 0 || count > 64)
            return;
        // Color_t RGBA @ 0x50 seulement — NE PAS ecrire @0x40 (pObjectInfo / data).
        const uint32_t col = static_cast<uint32_t>(r)
            | (static_cast<uint32_t>(g) << 8)
            | (static_cast<uint32_t>(b) << 16)
            | 0xFF000000u;
        for (int i = 0; i < count; ++i) {
            auto* m = mesh + static_cast<size_t>(i) * k_mesh_stride;
            if (!mem::valid(reinterpret_cast<uintptr_t>(m)))
                continue;
            if (mat) {
                *reinterpret_cast<void**>(m + k_mat0) = mat;
                *reinterpret_cast<void**>(m + k_mat1) = mat;
            }
            *reinterpret_cast<uint32_t*>(m + k_color) = col;
        }
    }

    // Peint [from,to). Stride FIXE 0x68 (DrawObject prouve).
    // Comme les bases premium : scene non-nul suffit (pas d'egalite object —
    // Peint [from,to). Stride GenPrim 0x70. draw_last = MESH_DRAW_FLAGS_DRAW_LAST
    static void paint_prim_range(PrimBuf* buf, int from, int to, void* mat,
                                 uint8_t r, uint8_t g, uint8_t b) {
        if (!buf || !buf->out || !mat || to <= from)
            return;
        if (buf->max_prims <= 0 || buf->max_prims > 8192)
            return;
        if (from < 0) from = 0;
        if (to > buf->max_prims) to = buf->max_prims;
        if (to <= from) return;
        if (to - from > 64)
            to = from + 64;
        if (!mem::valid(reinterpret_cast<uintptr_t>(buf->out)))
            return;

        const uint32_t col = static_cast<uint32_t>(r)
            | (static_cast<uint32_t>(g) << 8)
            | (static_cast<uint32_t>(b) << 16)
            | 0xFF000000u;
        __try {
            for (int i = from; i < to; ++i) {
                auto* m = buf->out + static_cast<size_t>(i) * k_gen_stride;
                if (!mem::valid(reinterpret_cast<uintptr_t>(m)))
                    break;
                const auto sc = *reinterpret_cast<void**>(m + k_scene);
                if (!sc)
                    continue;
                *reinterpret_cast<void**>(m + k_mat0) = mat;
                *reinterpret_cast<void**>(m + k_mat1) = mat;
                *reinterpret_cast<uint32_t*>(m + k_color) = col;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    struct MeshBackup {
        void* mat0{};
        void* mat1{};
        uint32_t color{};
    };

    static int snapshot_meshes(uint8_t* mesh, int count, MeshBackup* out, int max_out) {
        if (!mesh || count <= 0 || !out || max_out <= 0)
            return 0;
        if (count > max_out) count = max_out;
        if (count > 64) count = 64;
        for (int i = 0; i < count; ++i) {
            auto* m = mesh + static_cast<size_t>(i) * k_mesh_stride;
            out[i].mat0 = *reinterpret_cast<void**>(m + k_mat0);
            out[i].mat1 = *reinterpret_cast<void**>(m + k_mat1);
            out[i].color = *reinterpret_cast<uint32_t*>(m + k_color);
        }
        return count;
    }

    static void restore_meshes(uint8_t* mesh, int count, const MeshBackup* bak) {
        if (!mesh || count <= 0 || !bak)
            return;
        for (int i = 0; i < count; ++i) {
            auto* m = mesh + static_cast<size_t>(i) * k_mesh_stride;
            *reinterpret_cast<void**>(m + k_mat0) = bak[i].mat0;
            *reinterpret_cast<void**>(m + k_mat1) = bak[i].mat1;
            *reinterpret_cast<uint32_t*>(m + k_color) = bak[i].color;
        }
    }

    static void* create_material(const char* name, const char* kv3_text) {
        const HMODULE tier0 = GetModuleHandleW(L"tier0.dll");
        if (!tier0) return nullptr;

        using LoadStrFn = bool(__fastcall*)(void*, void*, const char*, const Kv3Id*, const char*, unsigned);
        using LoadBufFn = bool(__fastcall*)(void*, void*, void*, Kv3Id*, void*, void*, void*, void*, const char*);
        using UtlCtor = void(__fastcall*)(void*, int, int, int);
        using UtlPut = void(__fastcall*)(void*, const char*);
        using CreateMatFn = int64_t(__fastcall*)(void*, void*, const char*, void*, unsigned, unsigned);

        auto load_str = reinterpret_cast<LoadStrFn>(GetProcAddress(tier0,
            "?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z"));
        auto load_nohdr = reinterpret_cast<LoadStrFn>(GetProcAddress(tier0,
            "?LoadKV3Text_NoHeader@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEBDAEBUKV3ID_t@@2I@Z"));
        auto load_buf = reinterpret_cast<LoadBufFn>(GetProcAddress(tier0,
            "?LoadKV3@@YA_NPEAVKeyValues3@@PEAVCUtlString@@PEAVCUtlBuffer@@AEBUKV3ID_t@@PEBDI@Z"));
        auto ctor = reinterpret_cast<UtlCtor>(GetProcAddress(tier0,
            "??0CUtlBuffer@@QEAA@HHW4BufferFlags_t@0@@Z"));
        auto put = reinterpret_cast<UtlPut>(GetProcAddress(tier0,
            "?PutString@CUtlBuffer@@QEAAXPEBD@Z"));

        auto create = reinterpret_cast<CreateMatFn>(pattern::scan(L"materialsystem2.dll",
            "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 8B F2"));
        if (!create) return nullptr;

        void* block = VirtualAlloc(nullptr, 0x800, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!block) return nullptr;
        std::memset(block, 0, 0x800);
        void* kv = static_cast<uint8_t*>(block) + 0x100;

        Kv3Id ids[] = {
            { "generic", 0x41B818518343427Eull, 0xB5F447C23C0CDF8Cull },
            { "generic", 0x469806E97412167Cull, 0xE73790B53EE6F2AFull },
        };
        const char* body = std::strstr(kv3_text, "{");
        if (!body) body = kv3_text;
        bool loaded = false;
        for (auto& id : ids) {
            if (loaded) break;
            if (load_str) {
                __try { loaded = load_str(kv, nullptr, kv3_text, &id, "", 0); }
                __except (EXCEPTION_EXECUTE_HANDLER) { loaded = false; }
            }
            if (!loaded && load_nohdr) {
                __try { loaded = load_nohdr(kv, nullptr, body, &id, "", 0); }
                __except (EXCEPTION_EXECUTE_HANDLER) { loaded = false; }
            }
            if (!loaded && load_buf && ctor && put) {
                alignas(16) uint8_t raw[0x180]{};
                ctor(raw, 0, static_cast<int>(std::strlen(kv3_text) + 64), 1);
                put(raw, kv3_text);
                __try {
                    loaded = load_buf(kv, nullptr, raw, &id, nullptr, nullptr, nullptr, nullptr, "");
                } __except (EXCEPTION_EXECUTE_HANDLER) { loaded = false; }
            }
        }
        if (!loaded) {
            VirtualFree(block, 0, MEM_RELEASE);
            log_line("[!] LoadKV3 a echoue");
            return nullptr;
        }

        void* matsys = nullptr;
        using CI = void*(__cdecl*)(const char*, int*);
        if (const auto ci = reinterpret_cast<CI>(
                GetProcAddress(GetModuleHandleW(L"materialsystem2.dll"), "CreateInterface"))) {
            int code = 0;
            matsys = ci("VMaterialSystem2_001", &code);
        }

        StrongHandle handle{};
        __try {
            create(matsys, &handle, name, kv, 0, 1);
            if (!handle.binding)
                create(nullptr, &handle, name, kv, 0, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }

        void* inst = handle.instance();
        if (!inst && handle.binding)
            inst = reinterpret_cast<void*>(const_cast<StrongHandle::Binding*>(handle.binding));
        if (!inst || !mem::valid(reinterpret_cast<uintptr_t>(inst))) {
            log_line("[!] CreateMaterial handle nul");
            return nullptr;
        }
        char line[128]{};
        wsprintfA(line, "[+] material %s @ %p", name, inst);
        log_line(line);
        return inst;
    }

    // Style "plat" (2e image) : unlit opaque, sans specular/eclairage (= pas de platre brillant).
    // Une seule mat xqz pour le glow classic (couleur menu).
    static constexpr const char* k_vmat_xqz =
        R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_unlitgeneric.vfx"
    g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
    g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
    F_DISABLE_Z_WRITE = 1
    F_DISABLE_Z_BUFFERING = 1
    F_DISABLE_Z_PREPASS = 1
    F_PAINT_VERTEX_COLORS = 1
    F_RENDER_BACKFACES = 1
    F_TRANSLUCENT = 0
    g_vColorTint = [1.0, 1.0, 1.0, 1.0]
})";

    // Variante Z-on (reserve / fallback DrawObject).
    static constexpr const char* k_vmat_vis =
        R"(<!-- kv3 encoding:text:version{e21c7f3c-8a33-41c5-9977-a76d3a32aa0d} format:generic:version{7412167c-06e9-4698-aff2-e63eb59037e7} -->
{
    shader = "csgo_unlitgeneric.vfx"
    g_tColor = resource:"materials/dev/primary_white_color_tga_21186c76.vtex"
    g_tNormal = resource:"materials/default/default_normal_tga_7652cb.vtex"
    F_DISABLE_Z_WRITE = 0
    F_DISABLE_Z_BUFFERING = 0
    F_DISABLE_Z_PREPASS = 0
    F_PAINT_VERTEX_COLORS = 1
    F_RENDER_BACKFACES = 1
    F_TRANSLUCENT = 0
    g_vColorTint = [1.0, 1.0, 1.0, 1.0]
})";

    static bool ensure_materials() {
        if (g_mats_ok && g_mat_vis && g_mat_xqz)
            return true;
        // Noms v29 : force recreate (plus de character glossy).
        g_mat_xqz = create_material("materials/dev/cspt_chams_xqz29.vmat", k_vmat_xqz);
        g_mat_vis = create_material("materials/dev/cspt_chams_vis29.vmat", k_vmat_vis);
        g_mats_ok = (g_mat_vis != nullptr && g_mat_xqz != nullptr);
        log_line(g_mats_ok
            ? "[+] chams materials OK (unlit flat)"
            : "[!] chams materials incomplets");
        return g_mats_ok;
    }

    static void maybe_log_stats() {
        const DWORD now = GetTickCount();
        if (now - g_last_stat_ms < 1500)
            return;
        g_last_stat_ms = now;
        char line[160]{};
        wsprintfA(line, "[chams] mode=%s calls=%u owner=%u match=%u mats=%d allow=%d",
            g_use_genprim ? "GenPrim" : "DrawObj",
            g_stat_calls.exchange(0), g_stat_owner.exchange(0),
            g_stat_match.exchange(0), g_mats_ok ? 1 : 0, g_nallow);
        log_line(line);
    }

    static bool ent_for_scene_object(void* object, uint8_t& cr, uint8_t& cg, uint8_t& cb, uintptr_t& ent) {
        ent = 0;
        if (!object || !g_on.load(std::memory_order_relaxed) || !g_mats_ok)
            return false;
        ent = owner_from_scene(reinterpret_cast<uintptr_t>(object));
        const auto local = game::local_pawn();
        if (!ent || ent == local)
            return false;
        g_stat_owner.fetch_add(1, std::memory_order_relaxed);
        if (!find_target(ent, cr, cg, cb))
            return false;
        g_stat_match.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    static bool buf_ok(PrimBuf* buf) {
        if (!buf || !mem::valid(reinterpret_cast<uintptr_t>(buf)))
            return false;
        if (!buf->out || !mem::valid(reinterpret_cast<uintptr_t>(buf->out)))
            return false;
        if (buf->max_prims <= 0 || buf->max_prims > 8192)
            return false;
        if (buf->start_prim < 0 || buf->start_prim > buf->max_prims)
            return false;
        return true;
    }

    // Glow classic: 1x o_gen + paint couleur unique (xqz traverse murs). Pas de dual.
    static void __fastcall hk_gen_prim(void* desc, void* object, void* a3, PrimBuf* buf) {
        if (!o_gen) return;

        if (g_gen_depth > 0) {
            o_gen(desc, object, a3, buf);
            return;
        }

        g_stat_calls.fetch_add(1, std::memory_order_relaxed);
        if (g_logged_first.fetch_add(1) == 0)
            log_line("[chams] GeneratePrimitives FIRST call");

        uint8_t cr = 255, cg = 40, cb = 40;
        uintptr_t ent = 0;
        bool do_chams = false;
        __try {
            do_chams = ent_for_scene_object(object, cr, cg, cb, ent) && buf_ok(buf);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            do_chams = false;
        }

        if (!do_chams) {
            o_gen(desc, object, a3, buf);
            maybe_log_stats();
            return;
        }

        g_gen_depth = 1;
        __try {
            if (buf->max_prims - buf->start_prim < 8) {
                o_gen(desc, object, a3, buf);
                g_gen_depth = 0;
                maybe_log_stats();
                return;
            }

            static std::atomic<uint32_t> s_logged_match{ 0 };
            if (s_logged_match.fetch_add(1) == 0)
                log_line("[chams] GenPrim match glow classic");

            const int prev = buf->start_prim;
            o_gen(desc, object, a3, buf);
            if (buf_ok(buf) && buf->start_prim > prev)
                paint_prim_range(buf, prev, buf->start_prim, g_mat_xqz, cr, cg, cb);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log_line("[!] GenPrim SEH — pass safe");
        }
        g_gen_depth = 0;
        maybe_log_stats();
    }

    static void __fastcall hk_draw_object(void* a1, void* a2, uint8_t* mesh, int count,
                                          void* a5, void* a6, void* a7, void* a8) {
        if (!o_draw) return;

        // GenPrim premium actif: ne PAS re-peindre ici (cause des taches violet).
        if (g_use_genprim) {
            o_draw(a1, a2, mesh, count, a5, a6, a7, a8);
            return;
        }

        g_stat_calls.fetch_add(1, std::memory_order_relaxed);
        if (g_logged_first.fetch_add(1) == 0) {
            char line[128]{};
            wsprintfA(line, "[chams] DrawObject FALLBACK FIRST mesh=%p count=%d", mesh, count);
            log_line(line);
        }

        uint8_t cr = 255, cg = 40, cb = 40;
        bool do_chams = false;
        uintptr_t ent = 0;

        __try {
            if (g_on.load(std::memory_order_relaxed) && mesh && count > 0 && count <= 64 && g_mats_ok) {
                ent = owner_from_mesh(mesh);
                const auto local = game::local_pawn();
                if (ent && ent != local) {
                    g_stat_owner.fetch_add(1, std::memory_order_relaxed);
                    if (find_target(ent, cr, cg, cb)) {
                        do_chams = true;
                        g_stat_match.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            do_chams = false;
        }

        if (!do_chams) {
            o_draw(a1, a2, mesh, count, a5, a6, a7, a8);
            maybe_log_stats();
            return;
        }

        MeshBackup bak[64]{};
        int nbak = 0;
        __try {
            nbak = snapshot_meshes(mesh, count, bak, 64);
            paint_meshes(mesh, nbak, g_mat_xqz, cr, cg, cb);
            o_draw(a1, a2, mesh, count, a5, a6, a7, a8);
            restore_meshes(mesh, nbak, bak);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (nbak > 0)
                restore_meshes(mesh, nbak, bak);
            o_draw(a1, a2, mesh, count, a5, a6, a7, a8);
        }
        maybe_log_stats();
    }

    void publish(const uintptr_t* allow, int nallow, const uintptr_t*, int,
                 bool on, unsigned char r, unsigned char g, unsigned char b) {
        g_on.store(on, std::memory_order_relaxed);
        g_nallow = 0;
        if (!on || !allow || nallow <= 0)
            return;
        if (nallow > 64) nallow = 64;
        for (int i = 0; i < nallow; ++i) {
            g_allow[g_nallow].ent = allow[i];
            g_allow[g_nallow].r = r;
            g_allow[g_nallow].g = g;
            g_allow[g_nallow].b = b;
            ++g_nallow;
        }
    }

    void publish_colored(const uintptr_t* ents, const uint8_t* cols, int n, bool on) {
        g_on.store(on, std::memory_order_relaxed);
        g_nallow = 0;
        if (!on || !ents || !cols || n <= 0)
            return;
        if (n > 64) n = 64;
        for (int i = 0; i < n; ++i) {
            g_allow[g_nallow].ent = ents[i];
            g_allow[g_nallow].r = cols[i * 3 + 0];
            g_allow[g_nallow].g = cols[i * 3 + 1];
            g_allow[g_nallow].b = cols[i * 3 + 2];
            ++g_nallow;
        }
    }

    static bool install_hook(void* target, void* detour, void** original, const char* tag) {
        if (!target || !detour || !original)
            return false;
        allow_cfg(detour);
        const auto created = MH_CreateHook(target, detour, original);
        if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED) {
            char line[96]{};
            wsprintfA(line, "[!] MH_CreateHook %s (%d)", tag, (int)created);
            log_line(line);
            return false;
        }
        if (MH_EnableHook(target) != MH_OK) {
            MH_RemoveHook(target);
            char line[96]{};
            wsprintfA(line, "[!] MH_EnableHook %s failed", tag);
            log_line(line);
            return false;
        }
        return true;
    }

    // Trouve &vtable[4] (slot) + fonction GeneratePrimitives.
    static bool find_genprim_slot(void*** out_slot, void** out_fn) {
        if (out_slot) *out_slot = nullptr;
        if (out_fn) *out_fn = nullptr;

        const HMODULE mod = GetModuleHandleW(L"scenesystem.dll");
        if (!mod) return false;
        const auto base = reinterpret_cast<uint8_t*>(mod);
        const auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
        if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) return false;

        const char needle[] = ".?AVCAnimatableSceneObjectDesc@@";
        const size_t nlen = sizeof(needle) - 1;
        uintptr_t name_rva = 0;
        const auto section = IMAGE_FIRST_SECTION(nt);
        for (UINT i = 0; i < nt->FileHeader.NumberOfSections && !name_rva; ++i) {
            const auto start = base + section[i].VirtualAddress;
            const size_t size = section[i].Misc.VirtualSize;
            if (size < nlen) continue;
            for (size_t j = 0; j + nlen < size; ++j) {
                if (std::memcmp(start + j, needle, nlen) == 0) {
                    name_rva = section[i].VirtualAddress + j;
                    break;
                }
            }
        }
        if (!name_rva) return false;

        const uintptr_t td_rva = name_rva - 0x10;
        uintptr_t col_rva = 0;
        for (UINT i = 0; i < nt->FileHeader.NumberOfSections && !col_rva; ++i) {
            if (!(section[i].Characteristics & IMAGE_SCN_MEM_READ))
                continue;
            if (section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
                continue;
            const auto start = base + section[i].VirtualAddress;
            const size_t size = section[i].Misc.VirtualSize;
            for (size_t j = 0; j + 0x18 <= size; j += 4) {
                const auto* p = reinterpret_cast<const uint32_t*>(start + j);
                const uintptr_t here = section[i].VirtualAddress + j;
                if (p[0] == 1 && p[3] == static_cast<uint32_t>(td_rva)
                    && p[5] == static_cast<uint32_t>(here)) {
                    col_rva = here;
                    break;
                }
            }
        }
        if (!col_rva) return false;

        const uint64_t col_va = reinterpret_cast<uint64_t>(base + col_rva);
        for (UINT i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (!(section[i].Characteristics & IMAGE_SCN_MEM_READ))
                continue;
            const auto start = base + section[i].VirtualAddress;
            const size_t size = section[i].Misc.VirtualSize;
            for (size_t j = 0; j + 16 <= size; j += 8) {
                if (*reinterpret_cast<const uint64_t*>(start + j) != col_va)
                    continue;
                void** vt = reinterpret_cast<void**>(start + j + 8);
                void* fn = vt[4];
                if (!fn || !mem::valid(reinterpret_cast<uintptr_t>(fn)))
                    continue;
                const auto* b = reinterpret_cast<const uint8_t*>(fn);
                if (b[0] == 0x48 && b[1] == 0x8B && b[2] == 0xC4) {
                    if (out_slot) *out_slot = &vt[4];
                    if (out_fn) *out_fn = fn;
                    char line[128]{};
                    wsprintfA(line, "[+] GenPrim slot=%p fn=%p", &vt[4], fn);
                    log_line(line);
                    return true;
                }
            }
        }
        return false;
    }

    static bool install_vtable_hook(void** slot, void* detour, void** original) {
        if (!slot || !detour || !original || !*slot)
            return false;
        allow_cfg(detour);
        DWORD old = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old))
            return false;
        *original = *slot;
        *slot = detour;
        VirtualProtect(slot, sizeof(void*), old, &old);
        return true;
    }

    static void uninstall_vtable_hook() {
        if (!g_gen_vt_slot || !g_gen_vt_orig)
            return;
        DWORD old = 0;
        if (VirtualProtect(g_gen_vt_slot, sizeof(void*), PAGE_READWRITE, &old)) {
            *g_gen_vt_slot = g_gen_vt_orig;
            VirtualProtect(g_gen_vt_slot, sizeof(void*), old, &old);
        }
        g_gen_vt_slot = nullptr;
        g_gen_vt_orig = nullptr;
    }

    static bool install_draw_object_hook() {
        if (o_draw)
            return true;
        const auto hit = pattern::scan(L"scenesystem.dll", "48 8B C4 53 57 41 54");
        if (!hit || !mem::valid(hit)) {
            log_line("[!] DrawObject introuvable");
            return false;
        }
        if (!install_hook(reinterpret_cast<void*>(hit),
                reinterpret_cast<void*>(&hk_draw_object),
                reinterpret_cast<void**>(&o_draw), "DrawObject"))
            return false;
        g_hook_draw_addr = reinterpret_cast<void*>(hit);
        char line[96]{};
        wsprintfA(line, "[+] DrawObject hooked @ %p mats=%d",
            reinterpret_cast<void*>(hit), g_mats_ok ? 1 : 0);
        log_line(line);
        return true;
    }

    void ensure_hooks() {
        if (g_hooks || g_tried)
            return;
        g_tried = true;

        ensure_materials();

        // Premium: GenPrim dual UNIQUEMENT (UC). DrawObject en complement = taches.
        void** slot = nullptr;
        void* fn = nullptr;
        if (find_genprim_slot(&slot, &fn) && fn) {
            if (install_hook(fn, reinterpret_cast<void*>(&hk_gen_prim),
                    reinterpret_cast<void**>(&o_gen), "GenPrim")) {
                g_hook_addr = fn;
                g_use_genprim = true;
                g_hooks = true;
                char line[128]{};
                wsprintfA(line, "[+] GenPrim PREMIUM MinHook @ %p mats=%d",
                    fn, g_mats_ok ? 1 : 0);
                log_line(line);
                return;
            }
            if (slot && install_vtable_hook(slot, reinterpret_cast<void*>(&hk_gen_prim),
                    &g_gen_vt_orig)) {
                o_gen = reinterpret_cast<GenPrimFn>(g_gen_vt_orig);
                g_gen_vt_slot = slot;
                g_hook_addr = fn;
                g_use_genprim = true;
                g_hooks = true;
                char line[128]{};
                wsprintfA(line, "[+] GenPrim PREMIUM vtable @ %p mats=%d",
                    fn, g_mats_ok ? 1 : 0);
                log_line(line);
                return;
            }
            log_line("[!] GenPrim hook rate — fallback DrawObject");
        } else {
            log_line("[!] GenPrim introuvable — fallback DrawObject");
        }

        if (install_draw_object_hook()) {
            g_hooks = true;
            g_hook_addr = g_hook_draw_addr;
            g_use_genprim = false;
        } else {
            log_line("[!] aucun hook chams");
        }
    }

    bool mesh_glow_ready() { return g_hooks && g_mats_ok; }

    void shutdown_hooks() {
        uninstall_vtable_hook();
        if (g_hook_addr && g_hook_addr != g_hook_draw_addr) {
            MH_DisableHook(g_hook_addr);
            MH_RemoveHook(g_hook_addr);
        }
        if (g_hook_draw_addr) {
            MH_DisableHook(g_hook_draw_addr);
            MH_RemoveHook(g_hook_draw_addr);
        }
        g_hook_addr = nullptr;
        g_hook_draw_addr = nullptr;
        o_gen = nullptr;
        o_draw = nullptr;
        g_use_genprim = false;
        g_hooks = false;
        g_tried = false;
        g_nallow = 0;
        g_on.store(false, std::memory_order_relaxed);
    }
}
