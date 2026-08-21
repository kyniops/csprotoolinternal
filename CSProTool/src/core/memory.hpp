#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace mem {
    bool valid(uintptr_t address);
    bool read_raw(uintptr_t address, void* buffer, size_t size);
    bool write_raw(uintptr_t address, const void* buffer, size_t size);

    template <typename T>
    inline T read(uintptr_t address) {
        T value{};
        read_raw(address, &value, sizeof(T));
        return value;
    }

    template <typename T>
    inline bool write(uintptr_t address, const T& value) {
        return write_raw(address, &value, sizeof(T));
    }

    template <typename T>
    inline T read_chain(uintptr_t base, std::initializer_list<uintptr_t> offsets) {
        uintptr_t cur = base;
        auto it = offsets.begin();
        for (; it != offsets.end(); ++it) {
            if (!valid(cur))
                return T{};
            if (std::next(it) == offsets.end())
                return read<T>(cur + *it);
            cur = read<uintptr_t>(cur + *it);
        }
        return T{};
    }
}