#pragma once
#include <Windows.h>
#include <cstdint>
#include <vector>

namespace pattern {
    inline std::vector<int> parse(const char* ida_pattern) {
        std::vector<int> bytes;
        for (const char* p = ida_pattern; *p; ++p) {
            if (*p == ' ' || *p == '\t') continue;
            if (*p == '?') {
                ++p;
                if (*p == '?') ++p;
                bytes.push_back(-1);
                continue;
            }
            char buf[3]{ p[0], p[1], 0 };
            bytes.push_back(static_cast<int>(strtoul(buf, nullptr, 16)));
            if (p[1]) ++p;
        }
        return bytes;
    }

    inline uintptr_t scan_region(const uint8_t* base, size_t size, const std::vector<int>& bytes) {
        if (!base || !size || bytes.empty() || bytes.size() > size)
            return 0;
        const size_t n = bytes.size();
        for (size_t i = 0; i + n <= size; ++i) {
            bool ok = true;
            for (size_t j = 0; j < n; ++j) {
                if (bytes[j] != -1 && base[i + j] != static_cast<uint8_t>(bytes[j])) {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return reinterpret_cast<uintptr_t>(base + i);
        }
        return 0;
    }

    // Scan uniquement les sections executables (evite le freeze + faux positifs data).
    inline uintptr_t scan(const wchar_t* module_name, const char* ida_pattern) {
        const HMODULE mod = GetModuleHandleW(module_name);
        if (!mod) return 0;
        const auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(mod);
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(
            reinterpret_cast<uint8_t*>(mod) + dos->e_lfanew);
        if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) return 0;

        const auto bytes = parse(ida_pattern);
        if (bytes.empty()) return 0;

        const auto base = reinterpret_cast<uint8_t*>(mod);
        const auto section = IMAGE_FIRST_SECTION(nt);
        for (UINT i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (!(section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE))
                continue;
            const auto start = base + section[i].VirtualAddress;
            const size_t size = section[i].Misc.VirtualSize;
            if (const auto hit = scan_region(start, size, bytes))
                return hit;
        }
        return 0;
    }
}
