#include "core/memory.hpp"
#include <Windows.h>

namespace mem {
    bool valid(uintptr_t address) {
        return address >= 0x10000 && address < 0x7FFFFFFFFFFF;
    }

    bool read_raw(uintptr_t address, void* buffer, size_t size) {
        if (!valid(address) || !buffer || !size)
            return false;
        __try {
            std::memcpy(buffer, reinterpret_cast<void*>(address), size);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool write_raw(uintptr_t address, const void* buffer, size_t size) {
        if (!valid(address) || !buffer || !size)
            return false;
        __try {
            std::memcpy(reinterpret_cast<void*>(address), buffer, size);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
}