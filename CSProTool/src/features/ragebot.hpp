#pragma once
#include "sdk/math.hpp"
#include <cstdint>

namespace features {
    bool ragebot_on();
    // True si counter-strafe force ce tick (bhop doit ceder les wishmoves).
    bool ragebot_holding_stop();

    void ragebot_autostop_tick();
    float ragebot_hitchance(const Vec3& aim, uintptr_t target_pawn);
    // early-stop -> hitchance -> edge autoshoot -> multitap
    bool ragebot_allow_shot(const Vec3& aim, uintptr_t target_pawn);
    // True seulement le tick ou un nouveau tir silent doit etre applique (pas les holds).
    bool ragebot_wants_silent();
    void ragebot_on_silent(bool did_silent);
    void ragebot_idle();
    float ragebot_last_hc();

    void apply_rage_preset();
}
