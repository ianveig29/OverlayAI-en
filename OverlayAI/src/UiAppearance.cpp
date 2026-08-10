// ============================================================
// UiAppearance.cpp
// Controls the visual appearance of the menu: colors, text size, overall style.
// ============================================================

#include "UiAppearance.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
    constexpr UiAppearanceSettings kDefaults{};
    UiAppearanceSettings g_settings = kDefaults;

    std::filesystem::path GetAppearancePath() {
        wchar_t executablePath[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(
            nullptr, executablePath, static_cast<DWORD>(_countof(executablePath)));
        const std::filesystem::path base = length > 0
            ? std::filesystem::path(executablePath).parent_path()
            : std::filesystem::current_path();
        return base / L"configs" / L"appearance.cfg";
    }

    void SanitizeColor(float color[3], const float fallback[3]) {
        for (int component = 0; component < 3; ++component) {
            if (!std::isfinite(color[component]))
                color[component] = fallback[component];
            color[component] = std::clamp(color[component], 0.0f, 1.0f);
        }
    }

    void Sanitize(UiAppearanceSettings& settings) {
        SanitizeColor(settings.accent, kDefaults.accent);
        SanitizeColor(settings.background, kDefaults.background);
        SanitizeColor(settings.text, kDefaults.text);
    }

    void Persist() {
        const std::filesystem::path path = GetAppearancePath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream output(path, std::ios::trunc);
        if (!output) return;
        output << "accent " << g_settings.accent[0] << ' '
            << g_settings.accent[1] << ' ' << g_settings.accent[2] << '\n';
        output << "background " << g_settings.background[0] << ' '
            << g_settings.background[1] << ' ' << g_settings.background[2] << '\n';
        output << "text " << g_settings.text[0] << ' '
            << g_settings.text[1] << ' ' << g_settings.text[2] << '\n';
    }
}

void InitializeUiAppearance() {
    UiAppearanceSettings loaded = kDefaults;
    std::ifstream input(GetAppearancePath());
    std::string key;
    while (input >> key) {
        float* target = nullptr;
        if (key == "accent") target = loaded.accent;
        else if (key == "background") target = loaded.background;
        else if (key == "text") target = loaded.text;

        if (target) {
            input >> target[0] >> target[1] >> target[2];
        } else {
            std::string ignored;
            std::getline(input, ignored);
        }
    }
    Sanitize(loaded);
    g_settings = loaded;
}

const UiAppearanceSettings& GetUiAppearance() {
    return g_settings;
}

void SetUiAppearance(const UiAppearanceSettings& settings) {
    g_settings = settings;
    Sanitize(g_settings);
    Persist();
}

void ResetUiAppearance() {
    g_settings = kDefaults;
    Persist();
}
