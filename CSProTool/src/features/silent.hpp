#pragma once
#include "sdk/math.hpp"
#include "sdk/entity.hpp"
#include <atomic>

namespace silent {
    inline std::atomic<bool> ready{ false };
    inline std::atomic<bool> shooting{ false };
    inline std::atomic<int> screen_w{ 1920 };
    inline std::atomic<int> screen_h{ 1080 };
    inline Vec3 angles{};

    inline void set_screen(int w, int h) {
        if (w > 0) screen_w.store(w, std::memory_order_relaxed);
        if (h > 0) screen_h.store(h, std::memory_order_relaxed);
    }

    // Ne sert plus a figer la cam — conserve juste un angle pour debug/HUD.
    inline void set(const Vec3& a) {
        angles = a;
        angles.z = 0.f;
        ready.store(true, std::memory_order_release);
    }

    inline void clear() {
        ready.store(false, std::memory_order_release);
        shooting.store(false, std::memory_order_release);
    }

    inline bool get(Vec3& out) {
        if (!ready.load(std::memory_order_acquire))
            return false;
        out = angles;
        return true;
    }

    // NO-OP : ecrire dwViewAngles / input ici fige la souris (SSG injouable).
    inline void hide_camera() {}
}
