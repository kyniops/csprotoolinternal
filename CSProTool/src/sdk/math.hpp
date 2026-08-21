#pragma once
#include <cmath>
#include <cstdint>

struct Vec2 {
    float x{}, y{};
    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}
    float length() const { return std::sqrt(x * x + y * y); }
};

struct Vec3 {
    float x{}, y{}, z{};
    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }

    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float length2d() const { return std::sqrt(x * x + y * y); }

    Vec3 normalized() const {
        const float l = length();
        if (l <= 0.0001f) return {};
        return *this * (1.0f / l);
    }
};

struct ViewMatrix {
    float m[4][4]{};

    bool world_to_screen(const Vec3& world, Vec2& screen, int width, int height) const {
        const float w = m[3][0] * world.x + m[3][1] * world.y + m[3][2] * world.z + m[3][3];
        if (w < 0.01f)
            return false;
        const float inv = 1.0f / w;
        const float x = (m[0][0] * world.x + m[0][1] * world.y + m[0][2] * world.z + m[0][3]) * inv;
        const float y = (m[1][0] * world.x + m[1][1] * world.y + m[1][2] * world.z + m[1][3]) * inv;
        screen.x = (width * 0.5f) * (1.0f + x);
        screen.y = (height * 0.5f) * (1.0f - y);
        return screen.x >= -50.f && screen.x <= width + 50.f && screen.y >= -50.f && screen.y <= height + 50.f;
    }
};

inline Vec3 calc_angle(const Vec3& src, const Vec3& dst) {
    const Vec3 d = dst - src;
    const float hyp = d.length2d();
    Vec3 angles{};
    angles.x = -std::atan2(d.z, hyp) * (180.0f / 3.14159265f);
    angles.y = std::atan2(d.y, d.x) * (180.0f / 3.14159265f);
    angles.z = 0.0f;
    return angles;
}

inline void normalize_angles(Vec3& a) {
    if (a.x > 89.0f) a.x = 89.0f;
    if (a.x < -89.0f) a.x = -89.0f;
    while (a.y > 180.0f) a.y -= 360.0f;
    while (a.y < -180.0f) a.y += 360.0f;
    a.z = 0.0f;
}