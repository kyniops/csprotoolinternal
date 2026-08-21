#pragma once
#include <cstdint>
#include "../../generated/offsets.hpp"
#include "../../generated/client_dll.hpp"
#include "../../generated/buttons.hpp"

namespace schema {
    namespace off = cs2_dumper::offsets;
    namespace btn = cs2_dumper::buttons;
    using namespace cs2_dumper::schemas::client_dll;

    inline constexpr uintptr_t k_entity_list_offset = 0x10;
    inline constexpr uintptr_t k_entity_stride = 0x70;
    inline constexpr uintptr_t k_bone_array = CSkeletonInstance::m_modelState + 0x80; // 0x1C0
    inline constexpr uintptr_t k_input_angles = 0x688;
    // CCSGOInput (singleton a client+dwCSGOInput — NE PAS dereferencer)
    // bInThirdPerson @ 0x251, angThirdPersonAngles @ 0x258 (layout courant)
    inline constexpr uintptr_t k_in_thirdperson = 0x251;
    inline constexpr uintptr_t k_thirdperson_angles = 0x258;
}
