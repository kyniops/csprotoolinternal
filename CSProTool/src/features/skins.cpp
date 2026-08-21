#include "features/skins.hpp"
#include "features/config.hpp"
#include "sdk/entity.hpp"
#include "sdk/spread.hpp"
#include "sdk/schema.hpp"
#include "core/memory.hpp"
#include "core/modules.hpp"
#include "core/pattern.hpp"
#include "MinHook.h"
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace skins {
    static const Option k_ak[] = {
        { "Defaut", 0 },
        { "Inheritance", 1171 },
        { "Searing Rage", 1207 },
        { "Midnight Laminate", 1218 },
        { "Nouveau Rouge", 1309 },
        { "The Oligarch", 1352 },
        { "Breakthrough", 1358 },
        { "Aphrodite", 1397 },
        { "AUTOEXEC", 1449 },
    };
    static const Option k_awp[] = {
        { "Defaut", 0 },
        { "Fade", 1026 },
        { "LongDog", 1213 },
        { "Green Energy", 1280 },
        { "Ice Coaled", 1346 },
        { "The End", 1356 },
        { "Queen's Gambit", 1422 },
        { "Black Box", 1467 },
    };
    static const Option k_m4a4[] = {
        { "Defaut", 0 },
        { "Etch Lord", 1165 },
        { "Hellish", 1209 },
        { "Choppa", 1210 },
        { "Full Throttle", 1353 },
        { "Aeolian Dark", 1364 },
        { "Dark Operative", 1446 },
    };
    static const Option k_m4a1[] = {
        { "Defaut", 0 },
        { "Blue Phosphor", 1017 },
        { "Fade", 1177 },
        { "Stratosphere", 1216 },
        { "Solitude", 1338 },
        { "Liquidation", 1340 },
        { "Electrum", 1433 },
    };
    static const Option k_glock[] = {
        { "Defaut", 0 },
        { "Fade", 38 },
        { "Block-18", 1167 },
        { "Shinobu", 1208 },
        { "Glockingbird", 1282 },
        { "Mirror Mosaic", 1348 },
        { "Ghost Protocol", 1450 },
    };
    static const Option k_usp[] = {
        { "Defaut", 0 },
        { "Whiteout", 1065 },
        { "PC-GRN", 1186 },
        { "Royal Guard", 1217 },
        { "Tropical Breeze", 1284 },
        { "Silent Shot", 1431 },
        { "Spiral Glitch", 1451 },
    };
    static const Option k_deagle[] = {
        { "Defaut", 0 },
        { "Blaze", 37 },
        { "Hypnotic", 61 },
        { "Heat Treated", 1054 },
        { "Serpent Strike", 1189 },
        { "The Daily Deagle", 1360 },
        { "Eastern Enigma", 1458 },
    };
    static const Option k_ssg[] = {
        { "Defaut", 0 },
        { "Acid Fade", 253 },
        { "Dezastre", 1161 },
        { "Memorial", 1187 },
        { "Tiger Tear", 1289 },
        { "Blush Pour", 1316 },
        { "Calligrafaux", 1379 },
    };
    static const Option k_aug[] = {
        { "Defaut", 0 },
        { "Hot Rod", 33 },
        { "Anodized Navy", 197 },
        { "Amber Fade", 246 },
        { "Steel Sentinel", 1198 },
        { "Trigger Discipline", 1339 },
        { "Lapis Lazuli", 1464 },
    };
    static const Option k_famas[] = {
        { "Defaut", 0 },
        { "Bad Trip", 1184 },
        { "2A2F", 1202 },
        { "Vendetta", 1365 },
        { "Byproduct", 1393 },
        { "Snake Song", 1461 },
    };
    static const Option k_galil[] = {
        { "Defaut", 0 },
        { "Blue Titanium", 216 },
        { "Amber Fade", 246 },
        { "Rainbow Spoon", 1178 },
        { "Control", 1185 },
        { "Galigator", 1434 },
    };
    static const Option k_p250[] = {
        { "Defaut", 0 },
        { "Whiteout", 102 },
        { "Nevermore", 813 },
        { "Constructivist", 1212 },
        { "Kintsugi", 1420 },
        { "Lotus Imprint", 1455 },
    };
    static const Option k_p90[] = {
        { "Defaut", 0 },
        { "Wave Breaker", 1190 },
        { "Straight Dimes", 1199 },
        { "Aeolian Light", 1361 },
        { "Deathgaze", 1419 },
    };
    static const Option k_mac10[] = {
        { "Defaut", 0 },
        { "Fade", 38 },
        { "Gold Brick", 1025 },
        { "Derailment", 1204 },
        { "Cat Fight", 1349 },
        { "Arabesque Mosaic", 1454 },
    };
    static const Option k_mp9[] = {
        { "Defaut", 0 },
        { "Hot Rod", 33 },
        { "Hypnotic", 61 },
        { "Nexus", 1193 },
        { "Latte Rush", 1211 },
        { "Urban Sovereign", 1423 },
    };

    static const Weapon k_weapons[] = {
        { "AK-47", 7, k_ak, (int)(sizeof(k_ak) / sizeof(k_ak[0])) },
        { "AWP", 9, k_awp, (int)(sizeof(k_awp) / sizeof(k_awp[0])) },
        { "M4A4", 16, k_m4a4, (int)(sizeof(k_m4a4) / sizeof(k_m4a4[0])) },
        { "M4A1-S", 60, k_m4a1, (int)(sizeof(k_m4a1) / sizeof(k_m4a1[0])) },
        { "Glock-18", 4, k_glock, (int)(sizeof(k_glock) / sizeof(k_glock[0])) },
        { "USP-S", 61, k_usp, (int)(sizeof(k_usp) / sizeof(k_usp[0])) },
        { "Desert Eagle", 1, k_deagle, (int)(sizeof(k_deagle) / sizeof(k_deagle[0])) },
        { "SSG 08", 40, k_ssg, (int)(sizeof(k_ssg) / sizeof(k_ssg[0])) },
        { "AUG", 8, k_aug, (int)(sizeof(k_aug) / sizeof(k_aug[0])) },
        { "FAMAS", 10, k_famas, (int)(sizeof(k_famas) / sizeof(k_famas[0])) },
        { "Galil AR", 13, k_galil, (int)(sizeof(k_galil) / sizeof(k_galil[0])) },
        { "P250", 36, k_p250, (int)(sizeof(k_p250) / sizeof(k_p250[0])) },
        { "P90", 19, k_p90, (int)(sizeof(k_p90) / sizeof(k_p90[0])) },
        { "MAC-10", 17, k_mac10, (int)(sizeof(k_mac10) / sizeof(k_mac10[0])) },
        { "MP9", 34, k_mp9, (int)(sizeof(k_mp9) / sizeof(k_mp9[0])) },
    };
    static_assert((int)(sizeof(k_weapons) / sizeof(k_weapons[0])) <= k_max_weapons);

    const Weapon* catalog(int* count) {
        if (count) *count = (int)(sizeof(k_weapons) / sizeof(k_weapons[0]));
        return k_weapons;
    }

    static const Knife k_knives[] = {
        { "Bayonet", 500, 3933374535u, "weapons/models/knife/knife_bayonet/weapon_knife_bayonet.vmdl" },
        { "Classic", 503, 3787235507u, "weapons/models/knife/knife_css/weapon_knife_css.vmdl" },
        { "Flip", 505, 4046390180u, "weapons/models/knife/knife_flip/weapon_knife_flip.vmdl" },
        { "Gut", 506, 2047704618u, "weapons/models/knife/knife_gut/weapon_knife_gut.vmdl" },
        { "Karambit", 507, 1731408398u, "weapons/models/knife/knife_karambit/weapon_knife_karambit.vmdl" },
        { "M9 Bayonet", 508, 1638561588u, "weapons/models/knife/knife_m9/weapon_knife_m9.vmdl" },
        { "Huntsman", 509, 2282479884u, "weapons/models/knife/knife_tactical/weapon_knife_tactical.vmdl" },
        { "Falchion", 512, 3412259219u, "weapons/models/knife/knife_falchion/weapon_knife_falchion.vmdl" },
        { "Bowie", 514, 2511498851u, "weapons/models/knife/knife_bowie/weapon_knife_bowie.vmdl" },
        { "Butterfly", 515, 1353709123u, "weapons/models/knife/knife_butterfly/weapon_knife_butterfly.vmdl" },
        { "Shadow Daggers", 516, 4269888884u, "weapons/models/knife/knife_push/weapon_knife_push.vmdl" },
        { "Paracord", 517, 1105782941u, "weapons/models/knife/knife_cord/weapon_knife_cord.vmdl" },
        { "Survival", 518, 275962944u, "weapons/models/knife/knife_canis/weapon_knife_canis.vmdl" },
        { "Ursus", 519, 1338637359u, "weapons/models/knife/knife_ursus/weapon_knife_ursus.vmdl" },
        { "Navaja", 520, 3230445913u, "weapons/models/knife/knife_navaja/weapon_knife_navaja.vmdl" },
        { "Nomad", 521, 3206681373u, "weapons/models/knife/knife_outdoor/weapon_knife_outdoor.vmdl" },
        { "Stiletto", 522, 2595277776u, "weapons/models/knife/knife_stiletto/weapon_knife_stiletto.vmdl" },
        { "Talon", 523, 4029975521u, "weapons/models/knife/knife_talon/weapon_knife_talon.vmdl" },
        { "Skeleton", 525, 365028728u, "weapons/models/knife/knife_skeleton/weapon_knife_skeleton.vmdl" },
        { "Kukri", 526, 3845286452u, "weapons/models/knife/knife_kukri/weapon_knife_kukri.vmdl" },
    };

    const Knife* knife_catalog(int* count) {
        if (count) *count = (int)(sizeof(k_knives) / sizeof(k_knives[0]));
        return k_knives;
    }

    int weapon_index(uint16_t def) {
        for (int i = 0; i < (int)(sizeof(k_weapons) / sizeof(k_weapons[0])); ++i) {
            if (k_weapons[i].def == def) return i;
        }
        return -1;
    }

    static bool needs_alt_model(int paint);
    static int sanitize_paint(int paint);

    int paint_for(uint16_t def) {
        const int idx = weapon_index(def);
        if (idx < 0) return 0;
        const int paint = cfg::skins::paint[idx];
        if (paint <= 0) return 0;
        const auto& w = k_weapons[idx];
        for (int i = 0; i < w.option_count; ++i) {
            if (w.options[i].paint == paint)
                return paint;
        }
        return 0;
    }

    const char* current_weapon_name() {
        const auto pawn = game::local_pawn();
        if (!mem::valid(pawn)) return "aucune";
        const auto wpn = game::active_weapon(pawn);
        if (!mem::valid(wpn)) return "aucune";
        const int idx = weapon_index(spread::item_index(wpn));
        if (idx < 0) return "non supporte (armes only)";
        return k_weapons[idx].name;
    }

    void sync_menu_weapon() {
        const auto pawn = game::local_pawn();
        if (!mem::valid(pawn)) return;
        const auto wpn = game::active_weapon(pawn);
        if (!mem::valid(wpn)) return;
        const int idx = weapon_index(spread::item_index(wpn));
        if (idx >= 0)
            cfg::skins::ui_weapon = idx;
    }

    static char g_dbg[160] = "skins off";
    static char g_knife_dbg[192] = "knife off";
    static uintptr_t g_last_wpn = 0;
    static int g_last_paint = -1;
    static uintptr_t g_last_hud = 0;
    static uintptr_t g_cur_hud = 0;
    static uint32_t g_last_vm_h = 0;
    static uint64_t g_cur_mask = 2ull;
    static int g_last_deploy = 0;
    static int g_ag2 = 0;
    static uint64_t g_hud_mask = 0;
    static char g_mdl_used[48] = "-";
    static uintptr_t g_last_knife_wpn = 0;
    static uint16_t g_last_knife_def = 0;
    static int g_last_knife_paint = -1;
    static int g_last_knife_deploy = 0;
    static int g_knife_hud_n = 0;
    static bool g_subclass_ok = false;
    static bool g_knife_regen_ok = false;
    static bool g_knife_meshfn_ok = false;
    static bool g_knife_comp_ok = false;
    static bool g_comp_bad = false;
    static uintptr_t g_knife_hud_ent = 0;
    static uintptr_t g_knife_hud_child = 0;
    static int g_knife_mesh_dbg = 0;
    static int g_knife_attr_n = 0;
    const char* debug_line() { return g_dbg; }
    const char* knife_debug_line() { return g_knife_dbg; }

    void force_refresh() {
        g_last_wpn = 0;
        g_last_paint = -1;
        g_last_hud = 0;
        g_last_vm_h = 0;
        g_last_deploy = 0;
        g_last_knife_wpn = 0;
        g_last_knife_def = 0;
        g_last_knife_paint = -1;
        g_last_knife_deploy = 0;
    }

    static bool is_gun(uint16_t def) {
        if (def == 0) return false;
        if (def == 42 || def == 59 || def == 80) return false;
        if (def >= 500 && def < 600) return false;
        if (def >= 43 && def <= 49) return false;
        return weapon_index(def) >= 0;
    }

    static void log_sk(const char* m) {
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

    using MeshFn = void(__fastcall*)(void* node, uint64_t mask);
    using Regen1Fn = void(__fastcall*)(void* weapon, unsigned char a2);
    using RegenAllFn = void(__fastcall*)();
    using SetModelFn = std::int64_t(__fastcall*)(void* ent, const char* mdl);
    using UpdateSubclassFn = void(__fastcall*)(void* ent);
    using FSNFn = std::int64_t(__fastcall*)(void* thisptr, int stage);

    static MeshFn g_mesh = nullptr;
    static bool g_mesh_tried = false;
    static bool g_mesh_bad = false;
    static Regen1Fn g_regen = nullptr;
    static bool g_regen_tried = false;
    static bool g_regen_bad = false;
    static RegenAllFn g_regen_all = nullptr;
    static bool g_regen_all_tried = false;
    static bool g_regen_all_bad = false;
    static SetModelFn g_setmodel = nullptr;
    static bool g_setmodel_tried = false;
    static bool g_setmodel_bad = false;
    static UpdateSubclassFn g_update_subclass = nullptr;
    static bool g_subclass_tried = false;
    static bool g_subclass_bad = false;
    static FSNFn o_fsn = nullptr;
    static void* g_fsn_addr = nullptr;
    static bool g_fsn_ok = false;
    static int g_hud_n = 0;

    static void find_mesh() {
        if (g_mesh_tried) return;
        g_mesh_tried = true;
        const auto addr = pattern::scan(L"client.dll",
            "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 8D 99 ? ? ? ? 48 8B 71");
        if (!addr) return;
        g_mesh = reinterpret_cast<MeshFn>(addr);
        allow_cfg(g_mesh);
        log_sk("[+] SetMeshGroupMask");
    }

    static void find_setmodel() {
        if (g_setmodel_tried) return;
        g_setmodel_tried = true;
        const auto addr = pattern::scan(L"client.dll",
            "40 53 48 83 EC ? 48 8B D9 4C 8B C2 48 8B 0D ? ? ? ? 48 8D 54 24");
        if (!addr) return;
        g_setmodel = reinterpret_cast<SetModelFn>(addr);
        allow_cfg(g_setmodel);
        log_sk("[+] SetModel");
    }

    static void find_update_subclass() {
        if (g_subclass_tried) return;
        g_subclass_tried = true;
        const char* pats[] = {
            "40 53 48 83 EC 30 48 8B 41 10 48 8B D9 8B 50 30",
            "40 53 48 83 EC ? 48 8B 41 ? 48 8B D9 8B 50",
            "4C 8B DC 53 48 81 EC ? ? ? ? 48 8B 41",
        };
        for (auto* p : pats) {
            const auto addr = pattern::scan(L"client.dll", p);
            if (addr) {
                g_update_subclass = reinterpret_cast<UpdateSubclassFn>(addr);
                allow_cfg(g_update_subclass);
                g_subclass_ok = true;
                log_sk("[+] UpdateSubclass");
                return;
            }
        }
    }

    static bool read_model_name(uintptr_t ent, char* out, size_t n) {
        if (!out || n < 8) return false;
        out[0] = 0;
        const auto node = mem::read<uintptr_t>(ent + schema::C_BaseEntity::m_pGameSceneNode);
        if (!mem::valid(node)) return false;
        auto p = mem::read<uintptr_t>(
            node + schema::CSkeletonInstance::m_modelState + schema::CModelState::m_ModelName);
        p &= ~7ull;
        if (!mem::valid(p)) return false;
        if (!mem::read_raw(p, out, n - 1)) return false;
        out[n - 1] = 0;
        return out[0] > 32 && out[0] < 127;
    }

    static bool looks_vmdl(const char* s) {
        return s && s[0] > 32 && s[0] < 127
            && std::strstr(s, ".vmdl") && std::strstr(s, "weapon");
    }

    static bool ent_is_knife(uintptr_t ent) {
        if (!mem::valid(ent)) return false;
        char dn[72]{};
        if (game::designer_name(ent, dn, sizeof(dn))) {
            if (std::strstr(dn, "knife") || std::strstr(dn, "bayonet"))
                return true;
        }
        char mdl[260]{};
        if (read_model_name(ent, mdl, sizeof(mdl)) && std::strstr(mdl, "knife"))
            return true;
        const auto d = spread::item_index(ent);
        if (d == 42 || d == 59 || d == 80) return true;
        if (d >= 500 && d < 600) return true;
        return false;
    }

    static bool is_knife_def(uint16_t def) {
        return def == 42 || def == 59 || def == 80 || (def >= 500 && def < 600);
    }

    static void patch_attrs(uintptr_t item, int paint, int seed, float wear);
    static bool is_legacy_paint(int paint);
    static uint64_t knife_mesh_mask(uint16_t def, int paint);

    static int attr_list_count(uintptr_t list) {
        const auto vec = list + schema::CAttributeList::m_Attributes;
        return mem::read<int>(vec);
    }

    static void find_mesh();
    static void find_regen();
    static void find_setmodel();
    static void find_update_subclass();
    static void call_regen_weapon(uintptr_t wpn);

    static void apply_knife_mesh_on_node(uintptr_t node, uint64_t mask, bool call_fn) {
        if (!mem::valid(node)) return;
        const auto addr = node + schema::CSkeletonInstance::m_modelState
            + schema::CModelState::m_MeshGroupMask;
        mem::write<uint64_t>(addr, mask);
        if (!call_fn || !g_mesh || g_mesh_bad) return;
        allow_cfg(g_mesh);
        __try {
            g_mesh(reinterpret_cast<void*>(node), mask);
            g_knife_meshfn_ok = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_mesh_bad = true;
            g_mesh = nullptr;
        }
    }

    static void patch_knife_econ(uintptr_t ent, const Knife& k, int paint, int seed,
        float wear, uint32_t xuid, bool set_def) {
        if (!mem::valid(ent)) return;
        const auto item = ent + schema::C_EconEntity::m_AttributeManager
            + schema::C_AttributeContainer::m_Item;
        mem::write<uint8_t>(item + schema::C_EconItemView::m_bInitialized, 0);
        mem::write<int>(item + schema::C_EconItemView::m_iItemIDHigh, -1);
        mem::write<int>(item + schema::C_EconItemView::m_iItemIDLow, -1);
        mem::write<uint64_t>(item + schema::C_EconItemView::m_iItemID, ~0ull);
        mem::write<uint32_t>(item + schema::C_EconItemView::m_iAccountID, xuid);
        if (set_def) {
            mem::write<uint16_t>(item + schema::C_EconItemView::m_iItemDefinitionIndex, k.def);
            mem::write<uint32_t>(ent + schema::C_BaseEntity::m_nSubclassID, k.subclass);
        }
        mem::write<int>(item + schema::C_EconItemView::m_iEntityQuality, 3);
        mem::write<uint8_t>(item + schema::C_EconItemView::m_bDisallowSOC, 0);
        mem::write<uint8_t>(item + schema::C_EconItemView::m_bRestoreCustomMaterialAfterPrecache, 1);
        mem::write<uint8_t>(ent + schema::C_EconEntity::m_bClientside, 1);
        mem::write<uint8_t>(ent + schema::C_EconEntity::m_bAttachmentDirty, 1);
        mem::write<int>(ent + schema::C_EconEntity::m_nFallbackPaintKit, paint);
        mem::write<int>(ent + schema::C_EconEntity::m_nFallbackSeed, seed);
        mem::write<float>(ent + schema::C_EconEntity::m_flFallbackWear, wear);
        mem::write<int>(ent + schema::C_EconEntity::m_nFallbackStatTrak, -1);
        if (paint > 0)
            patch_attrs(item, paint, seed, wear);
        g_knife_attr_n = attr_list_count(item + schema::C_EconItemView::m_AttributeList);
        mem::write<uint8_t>(item + schema::C_EconItemView::m_bInitialized, 1);
    }

    static uint64_t knife_mesh_mask(uint16_t def, int paint) {
        if (paint > 0) return 1ull;
        if (def == 42 || def == 59 || def == 526) return 1ull;
        return 2ull;
    }

    static void call_regen_weapon(uintptr_t wpn) {
        if (!g_regen || g_regen_bad || !mem::valid(wpn)) return;
        allow_cfg(g_regen);
        __try {
            g_regen(reinterpret_cast<void*>(wpn), 0);
            g_knife_regen_ok = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_regen_bad = true;
            g_regen = nullptr;
        }
    }

    static void call_setmodel_knife(uintptr_t ent, const char* mdl) {
        if (!g_setmodel || g_setmodel_bad || !mem::valid(ent) || !looks_vmdl(mdl)) return;
        allow_cfg(g_setmodel);
        __try {
            g_setmodel(reinterpret_cast<void*>(ent), mdl);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_setmodel_bad = true;
            g_setmodel = nullptr;
        }
    }

    static void call_regen_all() {
        if (!g_regen_all || g_regen_all_bad) return;
        allow_cfg(g_regen_all);
        __try {
            g_regen_all();
            g_knife_regen_ok = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_regen_all_bad = true;
            g_regen_all = nullptr;
        }
    }

    static void call_update_subclass(uintptr_t ent) {
        if (!g_update_subclass || g_subclass_bad || !mem::valid(ent)) return;
        allow_cfg(g_update_subclass);
        __try {
            g_update_subclass(reinterpret_cast<void*>(ent));
            g_subclass_ok = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_subclass_bad = true;
            g_update_subclass = nullptr;
        }
    }

    using CompFn = void(__fastcall*)(void*, int);
    static void call_update_composite(uintptr_t ent) {
        if (g_comp_bad || !mem::valid(ent)) return;
        const auto client = modules::client();
        const auto vt = mem::read<uintptr_t>(ent);
        if (!client || !vt || vt < client || vt > client + 0x08000000ull)
            return;
        const int idxs[] = { 7, 100 };
        for (int idx : idxs) {
            const auto fn = mem::read<uintptr_t>(vt + static_cast<uintptr_t>(idx) * 8ull);
            if (!fn || fn < client || fn > client + 0x08000000ull) {
                g_comp_bad = true;
                return;
            }
            allow_cfg(reinterpret_cast<void*>(fn));
            __try {
                reinterpret_cast<CompFn>(fn)(reinterpret_cast<void*>(ent), 1);
                g_knife_comp_ok = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_comp_bad = true;
                return;
            }
        }
    }

    static bool is_hud_weapon_ent(uintptr_t ent) {
        char dn[72]{};
        return game::designer_name(ent, dn, sizeof(dn)) && std::strstr(dn, "hudmodel_weapon");
    }

    static bool hud_owned_by_knife(uintptr_t hud, uintptr_t wpn) {
        if (!mem::valid(hud) || !mem::valid(wpn)) return false;
        const auto h = mem::read<uint32_t>(hud + schema::C_BaseEntity::m_hOwnerEntity);
        if (!h || h == 0xFFFFFFFFu) return false;
        return game::get_entity(h) == wpn;
    }

    static void apply_knife_hud(uintptr_t pawn, uintptr_t wpn, uint64_t mesh, bool call_fn) {
        g_knife_hud_n = 0;
        g_knife_hud_ent = 0;
        g_knife_hud_child = 0;
        const auto arms_h = mem::read<uint32_t>(pawn + schema::C_CSPlayerPawn::m_hHudModelArms);
        if (!arms_h || arms_h == 0xFFFFFFFFu) return;
        const auto arms = game::get_entity(arms_h);
        if (!mem::valid(arms)) return;
        const auto root = mem::read<uintptr_t>(arms + schema::C_BaseEntity::m_pGameSceneNode);
        auto child = mem::valid(root)
            ? mem::read<uintptr_t>(root + schema::CGameSceneNode::m_pChild)
            : 0;
        for (int n = 0; n < 16 && mem::valid(child); ++n) {
            const auto owner = mem::read<uintptr_t>(child + schema::CGameSceneNode::m_pOwner);
            if (mem::valid(owner) && is_hud_weapon_ent(owner) && hud_owned_by_knife(owner, wpn)) {
                ++g_knife_hud_n;
                g_knife_hud_ent = owner;
                g_knife_hud_child = child;
                apply_knife_mesh_on_node(child, mesh, call_fn);
            }
            child = mem::read<uintptr_t>(child + schema::CGameSceneNode::m_pNextSibling);
        }
    }

    static uintptr_t knife_viewmodel(uintptr_t wpn) {
        const auto h = mem::read<uint32_t>(wpn + schema::C_EconEntity::m_hViewmodelAttachment);
        if (!h || h == 0xFFFFFFFFu) return 0;
        const auto vm = game::get_entity(h);
        return mem::valid(vm) ? vm : 0;
    }

    static void apply_knife_entity(uintptr_t pawn, uintptr_t wpn, const Knife& k,
        int paint, int seed, float wear, bool allow_heavy) {
        if (!mem::valid(wpn)) return;
        if (wear < 0.01f) wear = 0.01f;
        if (wear > 0.99f) wear = 0.99f;

        const int deploy = mem::read<int>(wpn + schema::C_CSWeaponBase::m_nDeployTick);
        const uint16_t cur_def = spread::item_index(wpn);
        const bool need_subclass = cur_def != k.def;
        const bool model_changed = wpn != g_last_knife_wpn || k.def != g_last_knife_def;
        const bool paint_changed = paint != g_last_knife_paint;
        const bool heavy = allow_heavy && (model_changed || paint_changed);

        find_setmodel();
        find_update_subclass();
        find_regen();
        find_mesh();

        const uint64_t mesh = knife_mesh_mask(k.def, paint);
        g_knife_mesh_dbg = static_cast<int>(mesh);
        const auto xuid = mem::read<uint32_t>(wpn + schema::C_EconEntity::m_OriginalOwnerXuidLow);
        const auto vm = knife_viewmodel(wpn);

        __try {
            patch_knife_econ(wpn, k, paint, seed, wear, xuid, true);
            apply_knife_hud(pawn, wpn, mesh, false);

            const auto node = mem::read<uintptr_t>(wpn + schema::C_BaseEntity::m_pGameSceneNode);
            apply_knife_mesh_on_node(node, mesh, false);
            if (mem::valid(vm)) {
                const auto vm_node = mem::read<uintptr_t>(vm + schema::C_BaseEntity::m_pGameSceneNode);
                apply_knife_mesh_on_node(vm_node, mesh, false);
            }

            if (!heavy) return;

            g_last_knife_wpn = wpn;
            g_last_knife_def = k.def;
            g_last_knife_paint = paint;
            g_last_knife_deploy = deploy;
            g_knife_regen_ok = false;
            g_knife_meshfn_ok = false;
            g_knife_comp_ok = false;

            if (need_subclass)
                call_update_subclass(wpn);

            call_setmodel_knife(wpn, k.model);
            if (mem::valid(g_knife_hud_ent) && is_hud_weapon_ent(g_knife_hud_ent))
                call_setmodel_knife(g_knife_hud_ent, k.model);
            if (mem::valid(vm))
                call_setmodel_knife(vm, k.model);

            patch_knife_econ(wpn, k, paint, seed, wear, xuid, true);
            mem::write<int>(wpn + schema::C_EconEntity::m_nUnloadedModelIndex, -1);
            mem::write<uint8_t>(wpn + schema::C_CSWeaponBase::m_bVisualsDataSet, 0);

            apply_knife_hud(pawn, wpn, mesh, true);
            apply_knife_mesh_on_node(node, mesh, true);
            if (mem::valid(g_knife_hud_child))
                apply_knife_mesh_on_node(g_knife_hud_child, mesh, true);
            if (mem::valid(vm)) {
                const auto vm_node = mem::read<uintptr_t>(vm + schema::C_BaseEntity::m_pGameSceneNode);
                apply_knife_mesh_on_node(vm_node, mesh, true);
            }

            if (paint > 0) {
                call_update_composite(wpn);
                call_regen_weapon(wpn);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    static void run_knife(bool allow_heavy) {
        if (!cfg::skins::knife_enabled) {
            std::snprintf(g_knife_dbg, sizeof(g_knife_dbg), "knife off");
            return;
        }
        if (!game::world_ready() || !game::local_alive()) {
            std::snprintf(g_knife_dbg, sizeof(g_knife_dbg), "knife: hors partie/mort");
            return;
        }

        int nk = 0;
        const auto* knives = knife_catalog(&nk);
        if (!knives || nk <= 0) {
            std::snprintf(g_knife_dbg, sizeof(g_knife_dbg), "knife: catalogue vide");
            return;
        }
        if (cfg::skins::knife_index < 0 || cfg::skins::knife_index >= nk)
            cfg::skins::knife_index = 4;

        const auto& sel = knives[cfg::skins::knife_index];
        const auto pawn = game::local_pawn();
        if (!mem::valid(pawn)) {
            std::snprintf(g_knife_dbg, sizeof(g_knife_dbg), "knife: pas de pawn");
            return;
        }

        const auto ws = mem::read<uintptr_t>(pawn + schema::C_BasePlayerPawn::m_pWeaponServices);
        if (!mem::valid(ws)) {
            std::snprintf(g_knife_dbg, sizeof(g_knife_dbg), "knife: pas de weapons");
            return;
        }

        const int count = mem::read<int>(ws + schema::CPlayer_WeaponServices::m_hMyWeapons);
        const auto data = mem::read<uintptr_t>(ws + schema::CPlayer_WeaponServices::m_hMyWeapons + 8);
        if (!mem::valid(data) || count <= 0 || count > 64) {
            std::snprintf(g_knife_dbg, sizeof(g_knife_dbg), "knife: inventaire vide");
            return;
        }

        const int paint = 0;
        const int seed = 1;
        const float wear = 0.01f;
        bool applied = false;

        __try {
            const auto active = game::active_weapon(pawn);
            if (mem::valid(active) && is_knife_def(spread::item_index(active))) {
                apply_knife_entity(pawn, active, sel, paint, seed, wear, allow_heavy);
                applied = true;
            } else {
                for (int i = 0; i < count; ++i) {
                    const auto handle = mem::read<uint32_t>(data + static_cast<uintptr_t>(i) * 4ull);
                    if (!handle || handle == 0xFFFFFFFFu) continue;
                    const auto wpn = game::get_entity(handle);
                    if (!mem::valid(wpn)) continue;
                    const uint16_t def = spread::item_index(wpn);
                    if (!is_knife_def(def)) continue;
                    apply_knife_entity(pawn, wpn, sel, paint, seed, wear, allow_heavy);
                    applied = true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            std::snprintf(g_knife_dbg, sizeof(g_knife_dbg), "knife: exception");
            return;
        }

        if (!applied) {
            std::snprintf(g_knife_dbg, sizeof(g_knife_dbg), "knife: pas de couteau");
            return;
        }

        std::snprintf(g_knife_dbg, sizeof(g_knife_dbg),
            "knife: %s hud %d sm %s sub %s",
            sel.name, g_knife_hud_n,
            g_setmodel && !g_setmodel_bad ? "ok" : "no",
            g_update_subclass && !g_subclass_bad ? "ok" : "no");
    }

    static const char* gun_hint(uint16_t def) {
        switch (def) {
            case 1: return "deagle";
            case 7: return "ak47";
            case 9: return "awp";
            case 16: return "m4a4";
            case 60: return "m4a1_silencer";
            case 4: return "glock";
            case 61: return "usp_silencer";
            case 40: return "ssg08";
            case 8: return "aug";
            case 10: return "famas";
            case 13: return "galil";
            case 36: return "p250";
            case 19: return "p90";
            case 17: return "mac10";
            case 34: return "mp9";
            default: return nullptr;
        }
    }

    static bool model_matches_gun(uintptr_t ent, uint16_t def) {
        if (!mem::valid(ent) || ent_is_knife(ent)) return false;
        const auto d = spread::item_index(ent);
        if (d && is_gun(d) && d != def) return false;
        char mdl[260]{};
        if (!read_model_name(ent, mdl, sizeof(mdl))) return false;
        if (std::strstr(mdl, "knife")) return false;
        const char* h = gun_hint(def);
        return h && std::strstr(mdl, h);
    }

    static void call_setmodel_path(uintptr_t ent, const char* mdl, uint16_t def) {
        if (!g_setmodel || g_setmodel_bad || !mem::valid(ent) || !looks_vmdl(mdl)) return;
        if (ent_is_knife(ent)) return;
        const auto d = spread::item_index(ent);
        if (d && is_gun(d) && def && d != def) return;
        allow_cfg(g_setmodel);
        __try {
            g_setmodel(reinterpret_cast<void*>(ent), mdl);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_setmodel_bad = true;
            g_setmodel = nullptr;
        }
    }

    static const char* gun_mag_vmdl(uint16_t def) {
        if (def == 1) return "weapons/models/deagle/weapon_pist_deagle_mag.vmdl";
        return nullptr;
    }

    static const char* gun_vmdl(uint16_t def) {
        switch (def) {
            case 1: return "weapons/models/deagle/weapon_pist_deagle.vmdl";
            case 7: return "weapons/models/ak47/weapon_rif_ak47.vmdl";
            case 9: return "weapons/models/awp/weapon_snip_awp.vmdl";
            case 16: return "weapons/models/m4a4/weapon_rif_m4a4.vmdl";
            case 60: return "weapons/models/m4a1_silencer/weapon_rif_m4a1_silencer.vmdl";
            case 4: return "weapons/models/glock18/weapon_pist_glock18.vmdl";
            case 61: return "weapons/models/usp_silencer/weapon_pist_usp_silencer.vmdl";
            case 40: return "weapons/models/ssg08/weapon_snip_ssg08.vmdl";
            case 8: return "weapons/models/aug/weapon_rif_aug.vmdl";
            case 10: return "weapons/models/famas/weapon_rif_famas.vmdl";
            case 13: return "weapons/models/galilar/weapon_rif_galilar.vmdl";
            case 36: return "weapons/models/p250/weapon_pist_p250.vmdl";
            case 19: return "weapons/models/p90/weapon_smg_p90.vmdl";
            case 17: return "weapons/models/mac10/weapon_smg_mac10.vmdl";
            case 34: return "weapons/models/mp9/weapon_smg_mp9.vmdl";
            default: return nullptr;
        }
    }

    static bool read_res_name(uintptr_t base, char* out, size_t n) {
        if (!mem::valid(base) || !out || n < 16) return false;
        out[0] = 0;
        auto try_ptr = [&](uintptr_t p) -> bool {
            p &= ~7ull;
            if (!mem::valid(p)) return false;
            char buf[260]{};
            if (!mem::read_raw(p, buf, sizeof(buf) - 1)) return false;
            buf[sizeof(buf) - 1] = 0;
            if (!looks_vmdl(buf)) return false;
            std::snprintf(out, n, "%s", buf);
            return true;
        };
        char tmp[200]{};
        if (mem::read_raw(base, tmp, sizeof(tmp) - 1)) {
            tmp[sizeof(tmp) - 1] = 0;
            if (looks_vmdl(tmp)) {
                std::snprintf(out, n, "%s", tmp);
                return true;
            }
        }
        if (try_ptr(mem::read<uintptr_t>(base))) return true;
        if (try_ptr(mem::read<uintptr_t>(base + 8))) return true;
        return false;
    }

    static bool needs_alt_model(int paint) {
        if (paint <= 0) return false;
        switch (paint) {
            case 344: case 962: case 984: case 1142:
            case 1144: case 1222: case 1255: case 1146:
            case 1321: case 1233: case 1241: case 1256:
                return true;
            default:
                return false;
        }
    }

    static bool is_ag2_paint(int paint) {
        return paint > 0;
    }

    static bool is_legacy_paint(int) {
        return false;
    }

    static int sanitize_paint(int paint) {
        return needs_alt_model(paint) ? 0 : paint;
    }

    static const char* resolve_model(uintptr_t wpn, uint16_t def, int paint, char* scratch, size_t n) {
        scratch[0] = 0;
        char world[260]{}, ag2[260]{};
        const auto vdata = mem::read<uintptr_t>(wpn + schema::C_BaseEntity::m_nSubclassID + 8);
        if (mem::valid(vdata)) {
            read_res_name(vdata + schema::CBasePlayerWeaponVData::m_szWorldModel, world, sizeof(world));
            read_res_name(vdata + schema::CBasePlayerWeaponVData::m_szWorldModelAg2Override, ag2, sizeof(ag2));
        }
        if (is_legacy_paint(paint)) {
            if (world[0] && (!ag2[0] || std::strcmp(world, ag2) != 0)) {
                std::snprintf(scratch, n, "%s", world);
                return scratch;
            }
        } else if (ag2[0]) {
            std::snprintf(scratch, n, "%s", ag2);
            return scratch;
        }
        if (world[0]) {
            std::snprintf(scratch, n, "%s", world);
            return scratch;
        }
        return gun_vmdl(def);
    }

    static void call_mesh_fn(uintptr_t node) {
        if (!mem::valid(node)) return;
        find_mesh();
        if (!g_mesh || g_mesh_bad) return;
        allow_cfg(g_mesh);
        __try {
            g_mesh(reinterpret_cast<void*>(node), g_cur_mask);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_mesh_bad = true;
            g_mesh = nullptr;
        }
    }

    static void find_regen() {
        if (!g_regen_tried) {
            g_regen_tried = true;
            const char* pats[] = {
                "40 55 53 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 44 0F B6 FA 48 8B D9 BA ? ? ? ? 48 8D 0D",
                "40 55 53 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 44 0F B6 FA 48 8B D9",
                "40 55 53 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 44 0F B6 FA",
            };
            for (auto* p : pats) {
                const auto addr = pattern::scan(L"client.dll", p);
                if (addr) {
                    g_regen = reinterpret_cast<Regen1Fn>(addr);
                    allow_cfg(g_regen);
                    log_sk("[+] RegenWeaponSkin");
                    break;
                }
            }
        }
        if (!g_regen_all_tried) {
            g_regen_all_tried = true;
            const auto addr = pattern::scan(L"client.dll",
                "48 83 EC ? E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 8B 10");
            if (addr) {
                g_regen_all = reinterpret_cast<RegenAllFn>(addr);
                allow_cfg(g_regen_all);
                log_sk("[+] RegenerateWeaponSkins");
            }
        }
    }

    static void set_mesh_mask_node(uintptr_t node) {
        if (!mem::valid(node)) return;
        const auto addr = node + schema::CSkeletonInstance::m_modelState
            + schema::CModelState::m_MeshGroupMask;
        mem::write<uint64_t>(addr, g_cur_mask);
    }

    static void set_mesh_mask(uintptr_t ent) {
        const auto node = mem::read<uintptr_t>(ent + schema::C_BaseEntity::m_pGameSceneNode);
        set_mesh_mask_node(node);
        call_mesh_fn(node);
    }

    static void set_bodygroup_legacy(uintptr_t node, bool legacy) {
        if (!mem::valid(node)) return;
        const auto vec = node + schema::CSkeletonInstance::m_modelState
            + schema::CModelState::m_nBodyGroupChoices;
        const int n = mem::read<int>(vec);
        const auto data = mem::read<uintptr_t>(vec + 8);
        if (!mem::valid(data) || n <= 0 || n > 16) return;
        mem::write<int>(data, legacy ? 1 : 0);
    }

    static bool hud_owned_by_weapon(uintptr_t hud, uintptr_t wpn) {
        if (!mem::valid(hud) || !mem::valid(wpn) || ent_is_knife(hud))
            return false;
        const auto h = mem::read<uint32_t>(hud + schema::C_BaseEntity::m_hOwnerEntity);
        if (!h || h == 0xFFFFFFFFu) return false;
        return game::get_entity(h) == wpn;
    }

    static void poke_ag2_legacy(uintptr_t, bool) {
    }

    static void apply_legacy_on_node(uintptr_t node, bool legacy) {
        if (!mem::valid(node)) return;
        set_mesh_mask_node(node);
        call_mesh_fn(node);
        set_bodygroup_legacy(node, legacy);
    }

    static void apply_legacy_on_ent(uintptr_t ent, uint16_t def, bool legacy) {
        if (!mem::valid(ent) || ent_is_knife(ent)) return;
        if (!model_matches_gun(ent, def) && spread::item_index(ent) != def)
            return;
        const auto node = mem::read<uintptr_t>(ent + schema::C_BaseEntity::m_pGameSceneNode);
        apply_legacy_on_node(node, legacy);
        poke_ag2_legacy(ent, legacy);
        auto child = mem::valid(node)
            ? mem::read<uintptr_t>(node + schema::CGameSceneNode::m_pChild)
            : 0;
        for (int n = 0; n < 8 && mem::valid(child); ++n) {
            const auto owner = mem::read<uintptr_t>(child + schema::CGameSceneNode::m_pOwner);
            if (mem::valid(owner) && !ent_is_knife(owner)
                && (model_matches_gun(owner, def) || owner == ent))
                apply_legacy_on_node(child, legacy);
            child = mem::read<uintptr_t>(child + schema::CGameSceneNode::m_pNextSibling);
        }
    }

    static void set_viewmodel_masks(uintptr_t pawn, uintptr_t wpn, uint16_t def) {
        g_hud_n = 0;
        g_cur_hud = 0;
        g_hud_mask = 0;
        g_ag2 = 0;
        const bool legacy = g_cur_mask == 2ull;

        apply_legacy_on_ent(wpn, def, legacy);

        const auto vm_h = mem::read<uint32_t>(wpn + schema::C_EconEntity::m_hViewmodelAttachment);
        const auto vm = (vm_h && vm_h != 0xFFFFFFFFu) ? game::get_entity(vm_h) : 0;
        if (model_matches_gun(vm, def))
            apply_legacy_on_ent(vm, def, legacy);

        const auto arms_h = mem::read<uint32_t>(pawn + schema::C_CSPlayerPawn::m_hHudModelArms);
        if (!arms_h || arms_h == 0xFFFFFFFFu) return;
        const auto arms = game::get_entity(arms_h);
        if (!mem::valid(arms)) return;
        const auto root = mem::read<uintptr_t>(arms + schema::C_BaseEntity::m_pGameSceneNode);
        auto child = mem::valid(root)
            ? mem::read<uintptr_t>(root + schema::CGameSceneNode::m_pChild)
            : 0;
        for (int n = 0; n < 16 && mem::valid(child); ++n) {
            const auto owner = mem::read<uintptr_t>(child + schema::CGameSceneNode::m_pOwner);
            if (mem::valid(owner) && !ent_is_knife(owner)) {
                char dn[72]{};
                const bool is_hud = game::designer_name(owner, dn, sizeof(dn))
                    && std::strstr(dn, "hudmodel_weapon");
                if (is_hud && hud_owned_by_weapon(owner, wpn)) {
                    ++g_hud_n;
                    if (!g_cur_hud)
                        g_cur_hud = owner;
                    apply_legacy_on_node(child, legacy);
                    apply_legacy_on_ent(owner, def, legacy);
                    poke_ag2_legacy(owner, legacy);
                    g_hud_mask = mem::read<uint64_t>(
                        child + schema::CSkeletonInstance::m_modelState
                        + schema::CModelState::m_MeshGroupMask);
                }
            }
            child = mem::read<uintptr_t>(child + schema::CGameSceneNode::m_pNextSibling);
        }
    }

    static void patch_attrs(uintptr_t item, int paint, int seed, float wear) {
        auto one = [&](uintptr_t list) {
            const auto vec = list + schema::CAttributeList::m_Attributes;
            const int n = mem::read<int>(vec);
            const auto data = mem::read<uintptr_t>(vec + 8);
            if (!mem::valid(data) || n <= 0 || n > 24) return;
            for (int i = 0; i < n; ++i) {
                const auto a = data + static_cast<uintptr_t>(i) * 0x48ull;
                const uint16_t def = mem::read<uint16_t>(
                    a + schema::CEconItemAttribute::m_iAttributeDefinitionIndex);
                float v = 0.f;
                if (def == 6) v = static_cast<float>(paint);
                else if (def == 7) v = static_cast<float>(seed);
                else if (def == 8) v = wear;
                else continue;
                mem::write<float>(a + schema::CEconItemAttribute::m_flValue, v);
                mem::write<float>(a + schema::CEconItemAttribute::m_flInitialValue, v);
            }
        };
        one(item + schema::C_EconItemView::m_AttributeList);
        one(item + schema::C_EconItemView::m_NetworkedDynamicAttributes);
    }

    // Regenerer le visuel d'une arme pendant qu'elle tire entre en collision avec la
    // mise a jour moteur de ses materiaux: on differe de quelques dizaines de ms.
    static float g_shot_stamp = -1.f;
    static DWORD g_shot_at = 0;

    static bool weapon_firing(uintptr_t wpn) {
        if (!mem::valid(wpn)) return false;
        const float t = mem::read<float>(wpn + schema::C_CSWeaponBase::m_fLastShotTime);
        if (t == t && t != g_shot_stamp) {
            g_shot_stamp = t;
            g_shot_at = GetTickCount();
        }
        return g_shot_at != 0 && GetTickCount() - g_shot_at < 300;
    }

    static void regen_weapon(uintptr_t pawn, uintptr_t wpn, int paint, uint16_t def) {
        if (!mem::valid(wpn) || !mem::valid(pawn) || !game::local_alive())
            return;
        if (wpn != game::active_weapon(pawn))
            return;
        g_cur_mask = is_legacy_paint(paint) ? 2ull : 1ull;
        const auto vm_h = mem::read<uint32_t>(wpn + schema::C_EconEntity::m_hViewmodelAttachment);
        const int deploy = mem::read<int>(wpn + schema::C_CSWeaponBase::m_nDeployTick);
        const bool hud_ok = g_hud_n > 0 && hud_owned_by_weapon(g_cur_hud, wpn);

        if (wpn == g_last_wpn && paint == g_last_paint
            && vm_h == g_last_vm_h && deploy == g_last_deploy) {
            if (!hud_ok) return;
            if (g_cur_hud == g_last_hud) return;
        }

        // Sortie sans memoriser l'etat: la regen sera refaite des la fin du tir.
        if (weapon_firing(wpn))
            return;

        g_last_wpn = wpn;
        g_last_paint = paint;
        g_last_vm_h = vm_h;
        g_last_deploy = deploy;
        g_last_hud = hud_ok ? g_cur_hud : 0;

        find_regen();
        find_mesh();
        find_setmodel();

        char scratch[260]{};
        const char* mdl = resolve_model(wpn, def, paint, scratch, sizeof(scratch));
        if (mdl) {
            const char* slash = std::strrchr(mdl, '/');
            std::snprintf(g_mdl_used, sizeof(g_mdl_used), "%s", slash ? slash + 1 : mdl);
        }

        mem::write<int>(wpn + schema::C_EconEntity::m_nUnloadedModelIndex, -1);
        mem::write<uint8_t>(wpn + schema::C_CSWeaponBase::m_bVisualsDataSet, 0);

        if (g_regen && !g_regen_bad) {
            allow_cfg(g_regen);
            __try {
                g_regen(reinterpret_cast<void*>(wpn), 0);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_regen_bad = true;
                g_regen = nullptr;
            }
        }

        const bool legacy = is_legacy_paint(paint);
        apply_legacy_on_ent(wpn, def, legacy);
        if (hud_ok)
            apply_legacy_on_ent(g_cur_hud, def, legacy);
    }

    static void apply_weapon(uintptr_t pawn, uintptr_t wpn, int paint, int seed, float wear, bool allow_regen) {
        if (!mem::valid(wpn) || paint <= 0) return;
        if (wear < 0.0001f) wear = 0.0001f;
        if (wear > 0.99f) wear = 0.99f;

        __try {
            const auto item = wpn + schema::C_EconEntity::m_AttributeManager
                + schema::C_AttributeContainer::m_Item;
            mem::write<uint8_t>(item + schema::C_EconItemView::m_bInitialized, 0);

            mem::write<int>(wpn + schema::C_EconEntity::m_nFallbackPaintKit, paint);
            mem::write<int>(wpn + schema::C_EconEntity::m_nFallbackSeed, seed);
            mem::write<float>(wpn + schema::C_EconEntity::m_flFallbackWear, wear);
            mem::write<int>(wpn + schema::C_EconEntity::m_nFallbackStatTrak, -1);
            mem::write<uint8_t>(wpn + schema::C_EconEntity::m_bAttachmentDirty, 1);
            mem::write<uint8_t>(wpn + schema::C_EconEntity::m_bClientside, 1);

            mem::write<uint64_t>(item + schema::C_EconItemView::m_iItemID, ~0ull);
            mem::write<int>(item + schema::C_EconItemView::m_iItemIDHigh, -1);
            mem::write<int>(item + schema::C_EconItemView::m_iItemIDLow, -1);
            const auto xuid = mem::read<uint32_t>(wpn + schema::C_EconEntity::m_OriginalOwnerXuidLow);
            mem::write<uint32_t>(item + schema::C_EconItemView::m_iAccountID, xuid);
            mem::write<int>(item + schema::C_EconItemView::m_iEntityQuality, 3);
            mem::write<uint8_t>(item + schema::C_EconItemView::m_bDisallowSOC, 1);
            patch_attrs(item, paint, seed, wear);
            mem::write<uint8_t>(item + schema::C_EconItemView::m_bInitialized, 1);

            if (allow_regen)
                regen_weapon(pawn, wpn, paint, spread::item_index(wpn));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    static void apply_inventory(uintptr_t pawn) {
        const auto ws = mem::read<uintptr_t>(pawn + schema::C_BasePlayerPawn::m_pWeaponServices);
        if (!mem::valid(ws)) return;

        const int count = mem::read<int>(ws + schema::CPlayer_WeaponServices::m_hMyWeapons);
        const auto data = mem::read<uintptr_t>(ws + schema::CPlayer_WeaponServices::m_hMyWeapons + 8);
        if (!mem::valid(data) || count <= 0 || count > 64) return;

        const int seed = cfg::skins::seed;
        const float wear = cfg::skins::wear;

        for (int i = 0; i < count; ++i) {
            const auto handle = mem::read<uint32_t>(data + static_cast<uintptr_t>(i) * 4ull);
            if (!handle || handle == 0xFFFFFFFFu) continue;
            const auto wpn = game::get_entity(handle);
            if (!mem::valid(wpn)) continue;
            const uint16_t def = spread::item_index(wpn);
            if (!is_gun(def)) continue;
            apply_weapon(pawn, wpn, paint_for(def), seed, wear, false);
        }
    }

    void on_world_lost() {
        force_refresh();
        g_cur_hud = 0;
        g_hud_n = 0;
        g_shot_stamp = -1.f;
        g_shot_at = 0;
    }

    void run(bool allow_regen) {
        if (!cfg::skins::enabled) {
            std::snprintf(g_dbg, sizeof(g_dbg), "skins off");
            return;
        }
        if (!game::world_ready() || !game::local_alive()) {
            std::snprintf(g_dbg, sizeof(g_dbg), "skins: hors partie/mort");
            return;
        }
        const auto pawn = game::local_pawn();
        if (!mem::valid(pawn)) {
            std::snprintf(g_dbg, sizeof(g_dbg), "skins: pas de pawn");
            return;
        }

        __try {
            sync_menu_weapon();
            apply_inventory(pawn);
            const auto active = game::active_weapon(pawn);
            if (!mem::valid(active)) {
                std::snprintf(g_dbg, sizeof(g_dbg), "skins: pas d'arme");
                return;
            }
            const uint16_t def = spread::item_index(active);
            const int want = paint_for(def);
            if (!is_gun(def)) {
                std::snprintf(g_dbg, sizeof(g_dbg), "skins: def %u non supporte", def);
                return;
            }
            if (want <= 0) {
                std::snprintf(g_dbg, sizeof(g_dbg), "skins: %s -- choisis un skin", current_weapon_name());
                return;
            }
            static uintptr_t last_active = 0;
            if (active != last_active) {
                g_last_wpn = 0;
                g_last_hud = 0;
                g_last_vm_h = 0;
                g_last_deploy = 0;
                last_active = active;
            }
            g_cur_mask = is_legacy_paint(want) ? 2ull : 1ull;
            g_ag2 = is_ag2_paint(want) ? 1 : 0;
            set_viewmodel_masks(pawn, active, def);
            apply_weapon(pawn, active, want, cfg::skins::seed, cfg::skins::wear, allow_regen);
            std::snprintf(g_dbg, sizeof(g_dbg),
                "skins: %s kit %d mesh%d ag2 %d hud %d %s",
                current_weapon_name(), want, (int)g_cur_mask, g_ag2, g_hud_n,
                g_hud_n > 0 ? "ok" : "wait");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            std::snprintf(g_dbg, sizeof(g_dbg), "skins: exception");
        }
    }

    static std::int64_t __fastcall hk_fsn(void* thisptr, int stage) {
        const bool ok = game::world_ready() && game::local_alive();
        if (ok && (stage == 5 || stage == 6) && cfg::skins::enabled) {
            __try { run(true); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (ok && (stage == 5 || stage == 6) && cfg::skins::knife_enabled) {
            __try { run_knife(true); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        const auto r = o_fsn ? o_fsn(thisptr, stage) : 0;
        if (ok && stage == 4 && cfg::skins::enabled) {
            __try { run(false); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (ok && stage == 4 && cfg::skins::knife_enabled) {
            __try { run_knife(false); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return r;
    }

    bool init_hooks() {
        if (g_fsn_ok) return true;
        const auto addr = pattern::scan(L"client.dll",
            "48 89 5C 24 ? 48 89 6C 24 ? 57 48 83 EC ? 48 8B F9 33 ED");
        if (!addr) return false;
        allow_cfg(reinterpret_cast<void*>(&hk_fsn));
        const auto created = MH_CreateHook(
            reinterpret_cast<void*>(addr),
            reinterpret_cast<void*>(&hk_fsn),
            reinterpret_cast<void**>(&o_fsn));
        if (created != MH_OK && created != MH_ERROR_ALREADY_CREATED)
            return false;
        if (MH_EnableHook(reinterpret_cast<void*>(addr)) != MH_OK) {
            MH_RemoveHook(reinterpret_cast<void*>(addr));
            return false;
        }
        g_fsn_addr = reinterpret_cast<void*>(addr);
        g_fsn_ok = true;
        find_mesh();
        find_regen();
        find_setmodel();
        find_update_subclass();
        log_sk("[+] FrameStageNotify skins");
        return true;
    }

    bool hooks_ready() { return g_fsn_ok; }

    void shutdown_hooks() {
        if (!g_fsn_ok) return;
        if (g_fsn_addr) {
            MH_DisableHook(g_fsn_addr);
            MH_RemoveHook(g_fsn_addr);
        }
        g_fsn_ok = false;
        g_fsn_addr = nullptr;
        o_fsn = nullptr;
    }
}
