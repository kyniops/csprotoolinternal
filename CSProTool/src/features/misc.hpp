#pragma once
namespace features {
    void run_fov_changer();
    void run_third_person();
    void run_atmosphere();
    void run_visual_removals();
    void shutdown_third_person();
    void on_world_lost();
    void run_anti_movement();

    void keep_scope_for_game();
    bool no_scope_crosshair();
}
