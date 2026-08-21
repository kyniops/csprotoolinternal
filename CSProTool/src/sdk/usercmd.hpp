#pragma once
#include "core/memory.hpp"
#include "core/modules.hpp"
#include "sdk/math.hpp"
#include <cstdint>
#include <cmath>

// Layout protobuf CS2 (Asphyxia / dumps 2024-2026) :
// on patche base.viewangles + CHAQUE input_history[i].view_angles
// et on met pitch/yaw delta des subticks a 0 (WASD analog inchange).

namespace usercmd {
    inline bool in_client(uintptr_t p) {
        const auto b = modules::client();
        return b && p > b && p < b + 0x08000000ull;
    }

    inline bool heap_obj(uintptr_t p) {
        return mem::valid(p) && !in_client(p);
    }

    inline bool looks_angle(float pitch, float yaw) {
        return std::isfinite(pitch) && std::isfinite(yaw)
            && pitch >= -89.5f && pitch <= 89.5f
            && yaw >= -180.5f && yaw <= 180.5f;
    }

    inline bool is_qangle_msg(uintptr_t msg) {
        if (!heap_obj(msg)) return false;
        const auto vt = mem::read<uintptr_t>(msg);
        if (!in_client(vt)) return false;
        const float x = mem::read<float>(msg + 0x18);
        const float y = mem::read<float>(msg + 0x1C);
        const float z = mem::read<float>(msg + 0x20);
        if (!looks_angle(x, y)) return false;
        if (!std::isfinite(z) || z < -180.5f || z > 180.5f)
            return false;
        return true;
    }

    // Angles seulement. Ne jamais toucher +0x8/+0xC :
    // sur CS2 ce sont des bit-range protobuf (GetBitRange). | 7 = crash.
    inline void write_qangle(uintptr_t msg, const Vec3& a) {
        if (!is_qangle_msg(msg)) return;
        if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(a.z))
            return;
        mem::write<float>(msg + 0x18, a.x);
        mem::write<float>(msg + 0x1C, a.y);
        mem::write<float>(msg + 0x20, a.z);
    }

    struct Repeated {
        int size = 0;
        uintptr_t rep = 0;
        int elem0 = 8;
        bool ok = false;
    };

    inline Repeated parse_repeated(uintptr_t field) {
        Repeated r{};
        if (!mem::valid(field)) return r;
        const int size = mem::read<int>(field + 8);
        const int total = mem::read<int>(field + 12);
        const auto rep = mem::read<uintptr_t>(field + 16);
        if (size < 1 || size > 32 || total < size || total > 64 || !heap_obj(rep))
            return r;
        for (const int off : { 8, 4 }) {
            const auto e0 = mem::read<uintptr_t>(rep + off);
            if (heap_obj(e0)) {
                r.size = size;
                r.rep = rep;
                r.elem0 = off;
                r.ok = true;
                return r;
            }
        }
        return r;
    }

    inline uintptr_t repeated_at(const Repeated& r, int i) {
        return mem::read<uintptr_t>(r.rep + r.elem0 + static_cast<uintptr_t>(i) * 8ull);
    }

    inline int patch_history(uintptr_t field, const Vec3& aim) {
        const auto r = parse_repeated(field);
        if (!r.ok) return 0;
        int n = 0;
        for (int i = 0; i < r.size; ++i) {
            const auto entry = repeated_at(r, i);
            if (!heap_obj(entry)) continue;
            const auto vt = mem::read<uintptr_t>(entry);
            if (!in_client(vt)) continue;
            const auto qang = mem::read<uintptr_t>(entry + 0x18);
            if (!is_qangle_msg(qang)) continue;
            write_qangle(qang, aim);
            ++n;
        }
        return n;
    }

    inline void patch_base_angles(uintptr_t base, const Vec3& aim) {
        if (!heap_obj(base)) return;
        const auto qang = mem::read<uintptr_t>(base + 0x40);
        if (is_qangle_msg(qang))
            write_qangle(qang, aim);
    }

    // Annule seulement les deltas d'angle des subticks. Laisse analog_forward/left (course).
    inline void zero_subtick_angle_deltas(uintptr_t base) {
        if (!heap_obj(base)) return;
        const auto r = parse_repeated(base + 0x18);
        if (!r.ok) return;
        for (int i = 0; i < r.size; ++i) {
            const auto step = repeated_at(r, i);
            if (!heap_obj(step)) continue;
            const auto vt = mem::read<uintptr_t>(step);
            if (!in_client(vt)) continue;
            const float pitch_d = mem::read<float>(step + 0x30);
            const float yaw_d = mem::read<float>(step + 0x34);
            if (!std::isfinite(pitch_d) || !std::isfinite(yaw_d))
                continue;
            if (std::fabs(pitch_d) <= 15.f && std::fabs(yaw_d) <= 15.f) {
                mem::write<float>(step + 0x30, 0.f);
                mem::write<float>(step + 0x34, 0.f);
            }
        }
    }

    struct CmdLayout {
        bool is_ptr = true;
        uintptr_t off = 0;
        uintptr_t hist_off = 0;
        uintptr_t base_off = 0;
        bool ready = false;
    };

    inline bool looks_like_cmd(uintptr_t cmd, uintptr_t hist_off, uintptr_t base_off) {
        if (!mem::valid(cmd)) return false;
        const auto hist = parse_repeated(cmd + hist_off);
        if (!hist.ok) return false;
        const auto entry = repeated_at(hist, 0);
        if (!heap_obj(entry)) return false;
        const auto qang = mem::read<uintptr_t>(entry + 0x18);
        if (!is_qangle_msg(qang)) return false;
        const auto base = mem::read<uintptr_t>(cmd + base_off);
        return heap_obj(base);
    }

    inline bool match_layout(uintptr_t cmd, CmdLayout& L, bool is_ptr, uintptr_t off) {
        static constexpr uintptr_t k_hist[] = { 0x28, 0x10, 0x30, 0x18 };
        static constexpr uintptr_t k_base[] = { 0x40, 0x28, 0x48, 0x30 };
        for (int i = 0; i < 4; ++i) {
            if (!looks_like_cmd(cmd, k_hist[i], k_base[i]))
                continue;
            L.is_ptr = is_ptr;
            L.off = off;
            L.hist_off = k_hist[i];
            L.base_off = k_base[i];
            L.ready = true;
            return true;
        }
        return false;
    }

    inline CmdLayout discover(uintptr_t input) {
        CmdLayout L{};
        for (size_t i = 0x20; i + 8 <= 0x680; i += 8) {
            const auto p = mem::read<uintptr_t>(input + i);
            if (heap_obj(p) && match_layout(p, L, true, i))
                return L;
        }
        for (size_t i = 0x20; i + 0x50 <= 0x700; i += 8) {
            if (i >= 0x680 && i < 0x698)
                continue;
            if (match_layout(input + i, L, false, i))
                return L;
        }
        return L;
    }

    inline uintptr_t resolve_cmd(uintptr_t input, const CmdLayout& L) {
        if (!L.ready || !mem::valid(input)) return 0;
        if (!L.is_ptr)
            return input + L.off;
        const auto p = mem::read<uintptr_t>(input + L.off);
        return heap_obj(p) ? p : 0;
    }

    inline int history_render_tick(uintptr_t cmd, uintptr_t hist_off) {
        const auto r = parse_repeated(cmd + hist_off);
        if (!r.ok) return 0;
        const auto entry = repeated_at(r, 0);
        if (!heap_obj(entry)) return 0;
        const int t = mem::read<int>(entry + 0x60);
        return (t > 0 && t < 100000000) ? t : 0;
    }

    inline int apply_to_cmd(uintptr_t cmd, const CmdLayout& L, const Vec3& a) {
        const int n = patch_history(cmd + L.hist_off, a);
        const auto base = mem::read<uintptr_t>(cmd + L.base_off);
        patch_base_angles(base, a);
        zero_subtick_angle_deltas(base);
        return n;
    }

    // SSG / scope : NE PAS patcher input_history (CS2 le recopierait sur la cam
    // et figerait la souris). Base + subticks suffisent pour le tir serveur.
    inline int apply_to_cmd_ssg(uintptr_t cmd, const CmdLayout& L, const Vec3& a) {
        const auto base = mem::read<uintptr_t>(cmd + L.base_off);
        patch_base_angles(base, a);
        zero_subtick_angle_deltas(base);
        return heap_obj(base) ? 1 : 0;
    }

    inline int read_base_seed(uintptr_t base) {
        if (!heap_obj(base)) return 0;
        for (const uintptr_t off : { 0x64ull, 0x68ull, 0x70ull, 0x5Cull }) {
            const int s = mem::read<int>(base + off);
            if (s != 0 && s != -1 && s != 0x7FFFFFFF)
                return s;
        }
        return 0;
    }

    inline bool peek_cmd(uintptr_t input, uintptr_t maybe_a3, uintptr_t& cmd, CmdLayout& L) {
        CmdLayout tmp{};
        if (maybe_a3 && match_layout(maybe_a3, tmp, true, 0)) {
            cmd = maybe_a3;
            L = tmp;
            return true;
        }
        static CmdLayout cached{};
        static int fails = 0;
        if (!cached.ready || fails > 8) {
            cached = discover(input);
            fails = 0;
        }
        if (!cached.ready) return false;
        cmd = resolve_cmd(input, cached);
        if (!cmd || !looks_like_cmd(cmd, cached.hist_off, cached.base_off)) {
            ++fails;
            cached.ready = false;
            return false;
        }
        L = cached;
        return true;
    }

    inline int peek_seed(uintptr_t cmd, const CmdLayout& L) {
        if (!cmd) return 0;
        return read_base_seed(mem::read<uintptr_t>(cmd + L.base_off));
    }

    inline int peek_tick(uintptr_t cmd, const CmdLayout& L) {
        if (!cmd) return 0;
        const int t = history_render_tick(cmd, L.hist_off);
        if (t) return t;
        const auto base = mem::read<uintptr_t>(cmd + L.base_off);
        if (!heap_obj(base)) return 0;
        const int tick = mem::read<int>(base + 0x4C);
        return (tick > 0 && tick < 100000000) ? tick : 0;
    }

    // Angles souris du cmd APRES o_create_move, AVANT silent.
    // Plus fiable que k_input_angles (souvent re-empoisonne par l'historique).
    inline bool peek_view_angles(uintptr_t input, uintptr_t maybe_a3, Vec3& out) {
        uintptr_t cmd = 0;
        CmdLayout L{};
        if (!peek_cmd(input, maybe_a3, cmd, L) || !cmd)
            return false;
        const auto base = mem::read<uintptr_t>(cmd + L.base_off);
        if (!heap_obj(base))
            return false;
        const auto qang = mem::read<uintptr_t>(base + 0x40);
        if (!is_qangle_msg(qang))
            return false;
        const float x = mem::read<float>(qang + 0x18);
        const float y = mem::read<float>(qang + 0x1C);
        const float z = mem::read<float>(qang + 0x20);
        if (!looks_angle(x, y) || !std::isfinite(z))
            return false;
        out = { x, y, z };
        return true;
    }

    inline int apply_silent_cmd(uintptr_t input, uintptr_t maybe_a3, const Vec3& aim) {
        static CmdLayout layout{};
        static int fails = 0;
        static int success_streak = 0;

        Vec3 a = aim;
        if (a.x > 89.f) a.x = 89.f;
        if (a.x < -89.f) a.x = -89.f;
        while (a.y > 180.f) a.y -= 360.f;
        while (a.y < -180.f) a.y += 360.f;

        CmdLayout tmp{};
        if (maybe_a3 && match_layout(maybe_a3, tmp, true, 0)) {
            const int n = apply_to_cmd(maybe_a3, tmp, a);
            if (n > 0) return n;
        }

        if (!layout.ready || fails > 3 || (success_streak < 2 && fails > 0)) {
            layout = discover(input);
            fails = 0;
        }
        if (!layout.ready)
            return 0;

        const auto cmd = resolve_cmd(input, layout);
        if (!cmd || !looks_like_cmd(cmd, layout.hist_off, layout.base_off)) {
            ++fails;
            success_streak = 0;
            layout.ready = false;
            return 0;
        }

        const int r = apply_to_cmd(cmd, layout, a);
        if (r > 0)
            ++success_streak;
        else
            success_streak = 0;
        return r;
    }

    
    // Silent SSG-safe : meme discovery que apply_silent_cmd, mais base-only.
    inline int apply_silent_cmd_ssg(uintptr_t input, uintptr_t maybe_a3, const Vec3& aim) {
        static CmdLayout layout{};
        static int fails = 0;

        Vec3 a = aim;
        if (a.x > 89.f) a.x = 89.f;
        if (a.x < -89.f) a.x = -89.f;
        while (a.y > 180.f) a.y -= 360.f;
        while (a.y < -180.f) a.y += 360.f;

        CmdLayout tmp{};
        if (maybe_a3 && match_layout(maybe_a3, tmp, true, 0)) {
            const int n = apply_to_cmd_ssg(maybe_a3, tmp, a);
            if (n > 0) return n;
        }

        if (!layout.ready || fails > 3) {
            layout = discover(input);
            fails = 0;
        }
        if (!layout.ready)
            return 0;

        const auto cmd = resolve_cmd(input, layout);
        if (!cmd || !looks_like_cmd(cmd, layout.hist_off, layout.base_off)) {
            ++fails;
            layout.ready = false;
            return 0;
        }

        const int r = apply_to_cmd_ssg(cmd, layout, a);
        if (r <= 0) {
            ++fails;
            return 0;
        }
        return r;
    }

inline void apply_wish_move(uintptr_t, uintptr_t, float, float) {
        // Ne jamais ecrire l'analog protobuf : offsets hasardeux = GetBitRange crash.
    }
}
