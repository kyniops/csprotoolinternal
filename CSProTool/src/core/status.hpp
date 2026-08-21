#pragma once
#include <atomic>

namespace tools {
    enum class St : int { Wait = 0, Ok = 1, Fail = 2 };

    inline std::atomic<int> client{ 0 };
    inline std::atomic<int> engine{ 0 };
    inline std::atomic<int> scene{ 0 };
    inline std::atomic<int> schema{ 0 };
    inline std::atomic<int> present{ 0 };
    inline std::atomic<int> imgui{ 0 };
    inline std::atomic<int> world{ 0 };
    inline std::atomic<int> silent_hook{ 0 };
    inline std::atomic<int> precision{ 0 };
    inline std::atomic<uint32_t> cm_ticks{ 0 };
    inline std::atomic<int> last_targets{ 0 };
    inline std::atomic<int> last_aim{ 0 }; // 0 none, 1 snap, 2 silent, 3 silent_move
    inline std::atomic<int> atm_ents{ 0 };
    inline std::atomic<int> atm_upd{ 0 };

    inline std::atomic<bool> running{ false };

    inline const char* label(int v) {
        if (v == static_cast<int>(St::Ok)) return "OK";
        if (v == static_cast<int>(St::Fail)) return "fail";
        return "attente";
    }

    inline void set(std::atomic<int>& s, St v) {
        s.store(static_cast<int>(v), std::memory_order_relaxed);
    }
}
