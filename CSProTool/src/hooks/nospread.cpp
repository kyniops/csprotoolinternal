#include "hooks/nospread.hpp"
#include "common.hpp"
#include <fstream>

namespace hooks {
    void tick_nospread() {}

    bool nospread_hook_active() { return false; }

    bool init_nospread() {
        std::ofstream f("C:\\Users\\Hugo\\Desktop\\csprotool_log.txt", std::ios::app);
        if (f) f << "[+] Precision: autoscope supprime, SSG droit a l'arret\n";
        return true;
    }

    void shutdown_nospread() {}
}
