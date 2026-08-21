#pragma once
#include <cstdint>

namespace weapons {
    inline const char* name(uint16_t def) {
        switch (def) {
            case 1: return "Deagle";
            case 2: return "Dualies";
            case 3: return "Five-SeveN";
            case 4: return "Glock";
            case 7: return "AK-47";
            case 8: return "AUG";
            case 9: return "AWP";
            case 10: return "FAMAS";
            case 11: return "G3SG1";
            case 13: return "Galil";
            case 14: return "M249";
            case 16: return "M4A4";
            case 17: return "MAC-10";
            case 19: return "P90";
            case 23: return "MP5";
            case 24: return "UMP-45";
            case 25: return "XM1014";
            case 26: return "Bizon";
            case 27: return "MAG-7";
            case 28: return "Negev";
            case 29: return "Sawed-Off";
            case 30: return "Tec-9";
            case 31: return "Zeus";
            case 32: return "P2000";
            case 33: return "MP7";
            case 34: return "MP9";
            case 35: return "Nova";
            case 36: return "P250";
            case 38: return "SCAR-20";
            case 39: return "SG 553";
            case 40: return "SSG 08";
            case 41: return "Couteau";
            case 42: return "Couteau";
            case 43: return "Flash";
            case 44: return "HE";
            case 45: return "Smoke";
            case 46: return "Molotov";
            case 47: return "Decoy";
            case 48: return "Incendiaire";
            case 49: return "C4";
            case 59: return "Couteau";
            case 60: return "M4A1-S";
            case 61: return "USP-S";
            case 63: return "CZ75";
            case 64: return "R8";
            default:
                if (def >= 500 && def < 600) return "Couteau";
                return nullptr;
        }
    }
}
