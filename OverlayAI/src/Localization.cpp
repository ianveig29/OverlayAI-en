// ============================================================
// Localization.cpp
// Translation system. Allows the menu to be displayed in different languages.
// ============================================================

#include "Localization.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {
    UiLanguage g_language = UiLanguage::Spanish;

    std::filesystem::path GetLanguagePath() {
        wchar_t executablePath[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(
            nullptr, executablePath, static_cast<DWORD>(_countof(executablePath)));
        std::filesystem::path base = length > 0
            ? std::filesystem::path(executablePath).parent_path()
            : std::filesystem::current_path();
        return base / L"configs" / L"language.cfg";
    }

    void PersistLanguage() {
        const std::filesystem::path path = GetLanguagePath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output(path, std::ios::trunc);
        if (output)
            output << (g_language == UiLanguage::English ? "en" : "es") << '\n';
    }
}

void InitializeLocalization() {
    std::ifstream input(GetLanguagePath());
    std::string value;
    if (input >> value)
        g_language = value == "en" ? UiLanguage::English : UiLanguage::Spanish;
}

UiLanguage GetUiLanguage() {
    return g_language;
}

void SetUiLanguage(UiLanguage language) {
    if (g_language == language) return;
    g_language = language;
    PersistLanguage();
}

const char* Localized(const char* spanish, const char* english) {
    return g_language == UiLanguage::English ? english : spanish;
}

