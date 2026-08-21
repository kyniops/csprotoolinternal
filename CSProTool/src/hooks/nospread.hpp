#pragma once

namespace hooks {
    bool init_nospread();
    void shutdown_nospread();
    bool nospread_hook_active();
    void tick_nospread(); // force scoped + zero penalty every frame
}
