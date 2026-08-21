#pragma once
#include <Windows.h>
#include <cstdint>
#include "core/status.hpp"

namespace modules {
    inline uintptr_t get(const wchar_t* name) {
        return reinterpret_cast<uintptr_t>(GetModuleHandleW(name));
    }

    inline uintptr_t client() { return get(L"client.dll"); }
    inline uintptr_t engine2() { return get(L"engine2.dll"); }
    inline uintptr_t scenesystem() { return get(L"scenesystem.dll"); }
    inline uintptr_t schemasystem() { return get(L"schemasystem.dll"); }
    inline uintptr_t inputsystem() { return get(L"inputsystem.dll"); }

    inline bool ready() {
        return client() != 0 && engine2() != 0;
    }

    inline bool core_ready() {
        return ready() && scenesystem() != 0;
    }

    // Attend un module jusqu'a timeout_ms. Retourne false si abandonne (END) ou timeout.
    inline bool wait_for(const wchar_t* name, std::atomic<int>* status, int timeout_ms) {
        if (status) tools::set(*status, tools::St::Wait);
        const DWORD start = GetTickCount();
        while (tools::running.load(std::memory_order_relaxed)) {
            if (get(name)) {
                if (status) tools::set(*status, tools::St::Ok);
                return true;
            }
            if (timeout_ms > 0 && (GetTickCount() - start) >= static_cast<DWORD>(timeout_ms))
                break;
            if (GetAsyncKeyState(VK_END) & 1)
                break;
            Sleep(120);
        }
        if (status && !get(name))
            tools::set(*status, tools::St::Fail);
        return get(name) != 0;
    }
}
