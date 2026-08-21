#pragma once
#include "core/memory.hpp"
#include "core/modules.hpp"
#include "core/pattern.hpp"
#include "sdk/schema.hpp"
#include "sdk/math.hpp"
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

// Compensation de spread CS2 (sans autostop, sans hook GetInaccuracy).
// Seed = SHA1(pitch quantifie, yaw, tick) comme l'engine. Le roll n'entre
// pas dans le hash : on l'utilise pour annuler le cone (roll-trick).

namespace spread {
    constexpr float k_pi = 3.14159265f;
    constexpr float k_rad = 180.f / k_pi;
    constexpr float k_deg = k_pi / 180.f;

    inline uintptr_t weapon_vdata(uintptr_t wpn) {
        if (!mem::valid(wpn)) return 0;
        return mem::read<uintptr_t>(wpn + schema::C_BaseEntity::m_nSubclassID + 0x8);
    }

    inline float mode_float(uintptr_t vdata, std::ptrdiff_t off, int mode) {
        return mem::read<float>(vdata + off + static_cast<std::ptrdiff_t>(mode) * 4);
    }

    inline uint16_t item_index(uintptr_t wpn) {
        if (!mem::valid(wpn)) return 0;
        return mem::read<uint16_t>(
            wpn + schema::C_EconEntity::m_AttributeManager
            + schema::C_AttributeContainer::m_Item
            + schema::C_EconItemView::m_iItemDefinitionIndex);
    }

    inline float reconstruct_inaccuracy(uintptr_t pawn, uintptr_t wpn) {
        const auto vdata = weapon_vdata(wpn);
        if (!mem::valid(vdata) || !mem::valid(pawn)) return 0.f;
        int mode = mem::read<int>(wpn + schema::C_CSWeaponBase::m_weaponMode);
        if (mode != 0 && mode != 1) mode = 0;

        const int flags = mem::read<int>(pawn + schema::C_BaseEntity::m_fFlags);
        const bool ground = (flags & 1) != 0;
        const bool duck = (flags & 2) != 0;

        float inacc = duck && ground
            ? mode_float(vdata, schema::CCSWeaponBaseVData::m_flInaccuracyCrouch, mode)
            : mode_float(vdata, schema::CCSWeaponBaseVData::m_flInaccuracyStand, mode);

        const auto vel = mem::read<Vec3>(pawn + schema::C_BaseEntity::m_vecAbsVelocity);
        float maxs = mode_float(vdata, schema::CCSWeaponBaseVData::m_flMaxSpeed, mode);
        if (maxs < 1.f) maxs = 250.f;
        const float ratio = vel.length2d() / maxs;
        const float move = (ratio < 1.f) ? ratio : 1.f;
        inacc += mode_float(vdata, schema::CCSWeaponBaseVData::m_flInaccuracyMove, mode) * move;

        if (!ground) {
            // CS2 : lerp takeoff -> apex. SSG quasi-laser au sommet du saut.
            const float jump = mode_float(vdata, schema::CCSWeaponBaseVData::m_flInaccuracyJump, mode);
            const float apex = mem::read<float>(vdata + schema::CCSWeaponBaseVData::m_flInaccuracyJumpApex);
            float t = std::fabs(vel.z) / 260.f;
            if (t > 1.f) t = 1.f;
            const float air = apex + (jump - apex) * t;
            inacc += (air > 0.f) ? air : 0.f;
        }

        inacc += mem::read<float>(wpn + schema::C_CSWeaponBase::m_fAccuracyPenalty);
        inacc += mem::read<float>(wpn + schema::C_CSWeaponBase::m_flTurningInaccuracy);
        if (inacc < 0.f) inacc = 0.f;
        if (inacc > 2.f) inacc = 2.f;
        return inacc;
    }

    inline float reconstruct_spread(uintptr_t wpn) {
        const auto vdata = weapon_vdata(wpn);
        if (!mem::valid(vdata)) return 0.f;
        int mode = mem::read<int>(wpn + schema::C_CSWeaponBase::m_weaponMode);
        if (mode != 0 && mode != 1) mode = 0;
        float s = mode_float(vdata, schema::CCSWeaponBaseVData::m_flSpread, mode);
        if (s < 0.f) s = 0.f;
        if (s > 1.f) s = 1.f;
        return s;
    }

    using AccFn = float(__fastcall*)(void*);
    inline AccFn resolve_inaccuracy() {
        static AccFn fn = nullptr;
        static bool tried = false;
        if (tried) return fn;
        tried = true;
        const auto addr = pattern::scan(L"client.dll",
            "48 89 5C 24 ? 57 48 81 EC ? ? ? ? 0F 29 7C 24 ?");
        if (addr)
            fn = reinterpret_cast<AccFn>(addr);
        return fn;
    }

    inline AccFn resolve_spread_fn() {
        static AccFn fn = nullptr;
        static bool tried = false;
        if (tried) return fn;
        tried = true;
        const auto addr = pattern::scan(L"client.dll", "48 83 EC 38 48 63 91 ? ? ? ?");
        if (addr)
            fn = reinterpret_cast<AccFn>(addr);
        return fn;
    }

    inline float live_spread(uintptr_t wpn) {
        return reconstruct_spread(wpn);
    }

    inline bool is_sniper(uintptr_t wpn) {
        const auto def = item_index(wpn);
        return def == 9 || def == 11 || def == 38 || def == 40;
    }

    inline float live_inaccuracy(uintptr_t pawn, uintptr_t wpn) {
        return reconstruct_inaccuracy(pawn, wpn);
    }

    // CUniformRandomStream (ran1) — Source SDK.
    namespace rng {
        constexpr int IA = 16807;
        constexpr int IM = 2147483647;
        constexpr int IQ = 127773;
        constexpr int IR = 2836;
        constexpr int NTAB = 32;
        constexpr int NDIV = (1 + (IM - 1) / NTAB);
        constexpr float AM = 1.0f / IM;
        constexpr float EPS = 1.2e-7f;
        constexpr float RNMX = 1.0f - EPS;

        inline int32_t& idum() { static int32_t v = 0; return v; }
        inline int32_t& iy() { static int32_t v = 0; return v; }
        inline int32_t* iv() { static int32_t a[NTAB]{}; return a; }

        inline void seed(int32_t s) {
            idum() = (s < 0) ? s : -s;
            if (idum() > -1) idum() = -1;
            iy() = 0;
        }

        inline int32_t generate() {
            int32_t& id = idum();
            int32_t& y = iy();
            auto* v = iv();
            if (id <= 0 || !y) {
                if (-id < 1) id = 1;
                else id = -id;
                for (int j = NTAB + 7; j >= 0; --j) {
                    const int k = id / IQ;
                    id = IA * (id - k * IQ) - IR * k;
                    if (id < 0) id += IM;
                    if (j < NTAB) v[j] = id;
                }
                y = v[0];
            }
            const int k = id / IQ;
            id = IA * (id - k * IQ) - IR * k;
            if (id < 0) id += IM;
            const int j = y / NDIV;
            y = v[j];
            v[j] = id;
            return y;
        }

        inline float flt(float lo, float hi) {
            float fl = generate() * AM;
            if (fl > RNMX) fl = RNMX;
            return lo + (hi - lo) * fl;
        }
    }

    inline float ang_norm(float a) {
        while (a > 180.f) a -= 360.f;
        while (a < -180.f) a += 360.f;
        return a;
    }

    inline float round_float(float x) {
        return static_cast<float>(static_cast<int>(x + (x >= 0.f ? 0.5f : -0.5f)));
    }

    // SHA-1 compact (public domain).
    inline uint32_t bswap32(uint32_t v) {
        return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
    }

    inline uint32_t sha1_rol(uint32_t v, int s) {
        return (v << s) | (v >> (32 - s));
    }

    inline uint32_t sha1_first_u32(const void* data, size_t len) {
        uint32_t h0 = 0x67452301u, h1 = 0xEFCDAB89u, h2 = 0x98BADCFEu;
        uint32_t h3 = 0x10325476u, h4 = 0xC3D2E1F0u;
        uint8_t block[64]{};
        const auto* src = static_cast<const uint8_t*>(data);
        if (len > 55) len = 55;
        std::memcpy(block, src, len);
        block[len] = 0x80;
        const uint64_t bits = static_cast<uint64_t>(len) * 8ull;
        block[63] = static_cast<uint8_t>(bits);
        block[62] = static_cast<uint8_t>(bits >> 8);

        uint32_t w[80]{};
        for (int i = 0; i < 16; ++i)
            w[i] = (block[i * 4] << 24) | (block[i * 4 + 1] << 16)
                 | (block[i * 4 + 2] << 8) | block[i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
            const uint32_t t = sha1_rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = sha1_rol(b, 30); b = a; a = t;
        }
        h0 += a;
        return bswap32(h0);
    }

    inline uint32_t compute_seed(const Vec3& ang, int tick) {
        float pitch = ang_norm(ang.x);
        pitch += pitch;
        pitch = round_float(pitch) * 0.5f;
        float yaw = ang_norm(ang.y);
        yaw += yaw;
        yaw = round_float(yaw) * 0.5f;
        unsigned char raw[12]{};
        std::memcpy(raw, &pitch, 4);
        std::memcpy(raw + 4, &yaw, 4);
        std::memcpy(raw + 8, &tick, 4);
        return sha1_first_u32(raw, 12);
    }

    inline void calc_spread(uint32_t seed, float inacc, float spr,
                            uint16_t item, float recoil,
                            float& out_x, float& out_y) {
        rng::seed(static_cast<int32_t>(seed) + 1);
        float dens0 = rng::flt(0.f, 1.f);
        if (item == 64) dens0 = 1.f - dens0 * dens0;
        else if (item == 28 && recoil < 3.f) {
            for (int n = 3; n > static_cast<int>(recoil); --n) dens0 *= dens0;
            dens0 = 1.f - dens0;
        }
        const float th0 = rng::flt(0.f, 2.f * k_pi);
        const float r0 = dens0 * inacc;
        const float x0 = r0 * std::cos(th0);
        const float y0 = r0 * std::sin(th0);

        float dens1 = rng::flt(0.f, 1.f);
        if (item == 64) dens1 = 1.f - dens1 * dens1;
        else if (item == 28 && recoil < 3.f) {
            for (int n = 3; n > static_cast<int>(recoil); --n) dens1 *= dens1;
            dens1 = 1.f - dens1;
        }
        const float th1 = rng::flt(0.f, 2.f * k_pi);
        const float r1 = dens1 * spr;
        const float x1 = r1 * std::cos(th1);
        const float y1 = r1 * std::sin(th1);

        out_x = x0 + x1;
        out_y = y0 + y1;
    }

    inline int client_tick() {
        const auto eng = modules::engine2();
        if (!eng) return 0;
        const auto nc = mem::read<uintptr_t>(
            eng + schema::off::engine2_dll::dwNetworkGameClient);
        if (!mem::valid(nc)) return 0;
        return mem::read<int>(nc + schema::off::engine2_dll::dwNetworkGameClient_clientTickCount);
    }

    inline void angle_vectors(const Vec3& a, Vec3& f, Vec3& r, Vec3& u) {
        const float sp = std::sin(a.x * k_deg), cp = std::cos(a.x * k_deg);
        const float sy = std::sin(a.y * k_deg), cy = std::cos(a.y * k_deg);
        const float sr = std::sin(a.z * k_deg), cr = std::cos(a.z * k_deg);
        f = { cp * cy, cp * sy, -sp };
        r = {
            -1.f * sr * sp * cy + -1.f * cr * -sy,
            -1.f * sr * sp * sy + -1.f * cr * cy,
            -1.f * sr * cp
        };
        u = {
            cr * sp * cy + -sr * -sy,
            cr * sp * sy + -sr * cy,
            cr * cp
        };
    }

    inline Vec3 vector_angles(const Vec3& f) {
        Vec3 a{};
        a.x = -std::atan2(f.z, std::sqrt(f.x * f.x + f.y * f.y)) * k_rad;
        a.y = std::atan2(f.y, f.x) * k_rad;
        return a;
    }

    inline Vec3 apply_spread_ang(const Vec3& ang, float sx, float sy) {
        Vec3 f, r, u;
        angle_vectors(ang, f, r, u);
        return vector_angles((f + r * sx + u * sy).normalized());
    }

    inline float ang_delta(const Vec3& a, const Vec3& b) {
        const float dp = a.x - b.x;
        const float dy = ang_norm(a.y - b.y);
        return std::sqrt(dp * dp + dy * dy);
    }

    // Seed = hash(CAMERA), pas l'angle silent. On inverse le cone sur la cible.
    inline Vec3 compensate(const Vec3& camera, const Vec3& want,
                           uintptr_t pawn, uintptr_t wpn, int tick, int cmd_seed) {
        Vec3 out = want;
        if (!mem::valid(pawn) || !mem::valid(wpn)) return out;

        const float inacc = live_inaccuracy(pawn, wpn);
        const float spr = live_spread(wpn);
        if (inacc + spr < 0.00005f) return out;

        if (tick <= 0) tick = client_tick();
        const uint32_t seed = cmd_seed
            ? static_cast<uint32_t>(cmd_seed)
            : compute_seed(camera, tick);
        const uint16_t item = item_index(wpn);
        const float recoil = mem::read<float>(wpn + schema::C_CSWeaponBase::m_flRecoilIndex);

        float sx = 0.f, sy = 0.f;
        calc_spread(seed, inacc, spr, item, recoil, sx, sy);
        out = apply_spread_ang(want, -sx, -sy);
        if (out.x > 89.f) out.x = 89.f;
        if (out.x < -89.f) out.x = -89.f;
        out.y = ang_norm(out.y);
        return out;
    }

    using GetInacc3Fn = float(__fastcall*)(void*, float*, float*);
    using SeedFn = uint32_t(__fastcall*)(void*, float*, uint32_t);
    using CalcSpreadFn = void(__fastcall*)(uint16_t, int, int, int, float, float, float, float*, float*);

    inline GetInacc3Fn resolve_inaccuracy3() {
        static GetInacc3Fn fn = nullptr;
        static bool tried = false;
        if (tried) return fn;
        tried = true;
        auto addr = pattern::scan(L"client.dll",
            "48 89 5C 24 10 55 56 57 48 81 EC B0 00 00 00 44 0F 29 84 24 80 00 00 00");
        if (!addr)
            addr = pattern::scan(L"client.dll",
                "48 89 5C 24 ? 55 56 57 48 81 EC ? ? ? ? 44 0F 29 84 24");
        if (addr)
            fn = reinterpret_cast<GetInacc3Fn>(addr);
        return fn;
    }

    inline SeedFn resolve_seed_fn() {
        static SeedFn fn = nullptr;
        static bool tried = false;
        if (tried) return fn;
        tried = true;
        const auto addr = pattern::scan(L"client.dll",
            "48 89 5C 24 08 57 48 81 EC F0 00 00 00 F3 0F 10 0A 48 8D 8C 24");
        if (addr)
            fn = reinterpret_cast<SeedFn>(addr);
        return fn;
    }

    inline CalcSpreadFn resolve_calc_spread() {
        static CalcSpreadFn fn = nullptr;
        static bool tried = false;
        if (tried) return fn;
        tried = true;
        const auto addr = pattern::scan(L"client.dll",
            "48 8B C4 48 89 58 ? 48 89 68 ? 48 89 70 ? 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 63 EA");
        if (addr)
            fn = reinterpret_cast<CalcSpreadFn>(addr);
        return fn;
    }

    inline float fire_inaccuracy(uintptr_t pawn, uintptr_t wpn, float* out_spread = nullptr) {
        if (out_spread)
            *out_spread = reconstruct_spread(wpn);
        return reconstruct_inaccuracy(pawn, wpn);
    }

    inline int controller_tick() {
        const auto client = modules::client();
        if (!client) return client_tick();
        const auto ctrl = mem::read<uintptr_t>(
            client + schema::off::client_dll::dwLocalPlayerController);
        if (!mem::valid(ctrl)) return client_tick();
        const int t = mem::read<int>(ctrl + schema::CBasePlayerController::m_nTickBase);
        return t > 0 ? t : client_tick();
    }

    inline uint32_t engine_seed(uintptr_t pawn, const Vec3& ang, uint32_t tick) {
        const auto fn = resolve_seed_fn();
        if (fn) {
            float a[3] = { ang.x, ang.y, ang.z };
            uint32_t s = 0;
            __try {
                s = fn(mem::valid(pawn) ? reinterpret_cast<void*>(pawn) : nullptr, a, tick);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                s = 0;
            }
            if (s) return s;
        }
        return compute_seed(ang, static_cast<int>(tick));
    }

    inline void engine_spread_xy(uint32_t crs_seed, uint16_t item, float inacc, float spr,
                                 float recoil, float& sx, float& sy) {
        sx = 0.f;
        sy = 0.f;
        const auto fn = resolve_calc_spread();
        if (fn) {
            __try {
                fn(item, 1, 0, static_cast<int>(crs_seed) + 1, inacc, spr, 0.f, &sx, &sy);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                sx = 0.f;
                sy = 0.f;
            }
            if (std::isfinite(sx) && std::isfinite(sy) && (sx != 0.f || sy != 0.f || inacc < 0.0001f))
                return;
        }
        calc_spread(crs_seed, inacc, spr, item, recoil, sx, sy);
    }

    // Methode 2026 : roll-trick + seed coherent (pitch bucket 0.5°).
    // Le protobuf silent est ignore : il faut ecrire la camera live (CCSGOInput).
    inline bool roll_straight(const Vec3& wish, uintptr_t pawn, uintptr_t wpn, Vec3& out) {
        out = wish;
        if (!mem::valid(pawn) || !mem::valid(wpn)) return false;

        float spr = live_spread(wpn);
        const float inacc = fire_inaccuracy(pawn, wpn, &spr);
        if (inacc + spr < 0.00005f) return false;

        const uint16_t item = item_index(wpn);
        const float recoil = mem::read<float>(wpn + schema::C_CSWeaponBase::m_flRecoilIndex);
        const int tb = controller_tick();
        const int ticks[3] = { tb, tb - 1, tb + 1 };
        const float baseB = std::floor(wish.x * 2.f) * 0.5f;

        auto make_adj = [&](float sx, float sy) {
            Vec3 adj = wish;
            adj.x += k_rad * std::atan(std::sqrt(sx * sx + sy * sy));
            adj.z = -k_rad * std::atan2(sx, sy);
            if (adj.x > 89.f) adj.x = 89.f;
            if (adj.x < -89.f) adj.x = -89.f;
            return adj;
        };

        for (int ti = 0; ti < 3; ++ti) {
            const int t = ticks[ti];
            if (t <= 0) continue;
            for (int k = -48; k <= 48; ++k) {
                const float cand = baseB + static_cast<float>(k) * 0.5f;
                if (cand > 89.f || cand < -89.f) continue;
                const uint32_t seed = engine_seed(pawn, { cand, wish.y, 0.f }, static_cast<uint32_t>(t));
                float sx = 0.f, sy = 0.f;
                engine_spread_xy(seed, item, inacc, spr, recoil, sx, sy);
                const Vec3 adj = make_adj(sx, sy);
                if (engine_seed(pawn, adj, static_cast<uint32_t>(t)) == seed) {
                    out = adj;
                    return true;
                }
            }
        }

        const uint32_t seed = engine_seed(pawn, { wish.x, wish.y, 0.f }, static_cast<uint32_t>(tb > 0 ? tb : 1));
        float sx = 0.f, sy = 0.f;
        engine_spread_xy(seed, item, inacc, spr, recoil, sx, sy);
        const Vec3 adj = make_adj(sx, sy);
        if (engine_seed(pawn, adj, static_cast<uint32_t>(tb > 0 ? tb : 1)) == seed) {
            out = adj;
            return true;
        }
        out = wish;
        return false;
    }

    // Recale verifie : apply_spread(angles) ~ wish. Sinon on ne pretend pas.
    inline bool straight_shot(const Vec3& wish, uintptr_t pawn, uintptr_t wpn, Vec3& out) {
        out = wish;
        if (!mem::valid(pawn) || !mem::valid(wpn)) return false;

        float spr = live_spread(wpn);
        const float inacc = fire_inaccuracy(pawn, wpn, &spr);
        if (inacc + spr < 0.00008f)
            return true;

        const uint16_t item = item_index(wpn);
        const float recoil = mem::read<float>(wpn + schema::C_CSWeaponBase::m_flRecoilIndex);
        const int tb = controller_tick();
        const int ticks[3] = { tb, tb - 1, tb + 1 };

        auto make_adj = [&](float sx, float sy) {
            Vec3 adj = wish;
            adj.x += k_rad * std::atan(std::sqrt(sx * sx + sy * sy));
            adj.z = -k_rad * std::atan2(sx, sy);
            if (adj.x > 89.f) adj.x = 89.f;
            if (adj.x < -89.f) adj.x = -89.f;
            return adj;
        };

        for (int ti = 0; ti < 3; ++ti) {
            const int t = ticks[ti];
            if (t <= 0) continue;

            const uint32_t seed = engine_seed(pawn, { wish.x, wish.y, 0.f }, static_cast<uint32_t>(t));
            float sx = 0.f, sy = 0.f;
            engine_spread_xy(seed, item, inacc, spr, recoil, sx, sy);

            Vec3 adj = make_adj(sx, sy);
            if (ang_delta(apply_spread_ang(adj, sx, sy), wish) < 0.12f) {
                out = adj;
                return true;
            }

            float best = 1.0e9f;
            Vec3 best_a = wish;
            for (int r = -180; r <= 180; r += 4) {
                Vec3 a = wish;
                a.z = static_cast<float>(r);
                const float d = ang_delta(apply_spread_ang(a, sx, sy), wish);
                if (d < best) {
                    best = d;
                    best_a = a;
                }
                if (d < 0.08f) {
                    out = a;
                    return true;
                }
            }
            if (best < 0.14f) {
                out = best_a;
                return true;
            }

            Vec3 it = wish;
            for (int i = 0; i < 6; ++i) {
                const uint32_t s = engine_seed(pawn, { it.x, it.y, 0.f }, static_cast<uint32_t>(t));
                float x = 0.f, y = 0.f;
                engine_spread_xy(s, item, inacc, spr, recoil, x, y);
                const Vec3 hit = apply_spread_ang(it, x, y);
                it.x -= (hit.x - wish.x);
                it.y = ang_norm(it.y - ang_norm(hit.y - wish.y));
                if (it.x > 89.f) it.x = 89.f;
                if (it.x < -89.f) it.x = -89.f;
                engine_spread_xy(
                    engine_seed(pawn, { it.x, it.y, 0.f }, static_cast<uint32_t>(t)),
                    item, inacc, spr, recoil, x, y);
                if (ang_delta(apply_spread_ang(it, x, y), wish) < 0.14f) {
                    out = it;
                    return true;
                }
            }
        }
        return false;
    }
}
