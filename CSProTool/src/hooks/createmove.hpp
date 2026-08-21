#pragma once
namespace hooks {
    bool init_createmove();
    void shutdown_createmove();
    bool createmove_active();
    unsigned createmove_ticks();
    float spinbot_yaw();
}
