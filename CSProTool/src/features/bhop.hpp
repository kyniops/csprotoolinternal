#pragma once
#include <cstdint>

namespace features {
    void run_bhop();
    void run_bhop_tick();
    void bhop_sync_cmd(void* input, std::int64_t a3);
    void run_no_ally_clip();
    void shutdown_no_ally_clip();
    void clear_ally_clip_cache();
}
