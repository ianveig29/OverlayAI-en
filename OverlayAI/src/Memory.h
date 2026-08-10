#pragma once

#include <windows.h>
#include <tlhelp32.h>
#include <cstddef>
#include <cstdint>

class ProcessMemory {
public:
    DWORD pid = 0;
    HANDLE hProcess = nullptr;
    uintptr_t clientModule = 0;
    size_t clientModuleSize = 0;

    ~ProcessMemory();

    bool Attach(const wchar_t* processName);

    template <typename T>
    T Read(uintptr_t address) {
        T buffer{};
        ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), &buffer, sizeof(T), nullptr);
        return buffer;
    }
    template <typename T>
    bool Write(uintptr_t address, const T& value) {
        return WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(address), &value, sizeof(T), nullptr) != FALSE;
    }
};

extern ProcessMemory mem;

bool IsValidPtr(uintptr_t p);
