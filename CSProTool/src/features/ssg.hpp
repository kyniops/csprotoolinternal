#pragma once
#include "sdk/math.hpp"

namespace features {
    void run_ssg();
    void run_ssg_tick();
    void ssg_flush_attack();
    void render_ssg_hud(int screen_w, int screen_h);
    bool ssg_want_spread();
    bool ssg_wish_ang(Vec3& out);
    void ssg_mark_head(bool hit);
    void ssg_abort_shot(const char* why);
    bool ssg_force_stop();
    bool ssg_block_jump();
    bool ssg_use_silent();
}
