#include "Memory.h"

ProcessMemory mem;

ProcessMemory::~ProcessMemory() {
    if (hProcess) CloseHandle(hProcess);
}

bool ProcessMemory::Attach(const wchar_t* processName) {
    PROCESSENTRY32W entry = { sizeof(PROCESSENTRY32W) };
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName) == 0) {
                pid = entry.th32ProcessID;
                hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (!hProcess) return false;

    MODULEENTRY32W modEntry = { sizeof(MODULEENTRY32W) };
    HANDLE modSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (modSnapshot != INVALID_HANDLE_VALUE) {
        if (Module32FirstW(modSnapshot, &modEntry)) {
            do {
                if (_wcsicmp(modEntry.szModule, L"client.dll") == 0) {
                    clientModule = reinterpret_cast<uintptr_t>(modEntry.modBaseAddr);
                    clientModuleSize = static_cast<size_t>(modEntry.modBaseSize);
                    break;
                }
            } while (Module32NextW(modSnapshot, &modEntry));
        }
        CloseHandle(modSnapshot);
    }
    return clientModule != 0;
}

bool IsValidPtr(uintptr_t p) {
    return p > 0x10000 && p < 0x7FFFFFFFFFFF;
}
