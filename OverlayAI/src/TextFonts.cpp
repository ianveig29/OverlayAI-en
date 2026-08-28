#include "TextFonts.h"

#include "../imgui.h"

#include <Windows.h>

#include "Config.h"

namespace {
    constexpr float kPlayerTextSize = 13.0f;
    constexpr float kUiTextSize = 16.0f;
    ImFont* g_classicFont = nullptr;
    ImFont* g_modernPlayerFont = nullptr;
    ImFont* g_modernUiFont = nullptr;

    bool FileExists(const char* path) {
        const DWORD attributes = GetFileAttributesA(path);
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    void MergeFont(ImFont* destination, const char* path,
        float size, const ImWchar* ranges) {
        if (!destination || !FileExists(path)) return;
        ImFontConfig config{};
        config.MergeMode = true;
        config.DstFont = destination;
        config.PixelSnapH = true;
        (void)ImGui::GetIO().Fonts->AddFontFromFileTTF(
            path, size, &config, ranges);
    }

    void MergeFallbacks(ImFont* destination, float size,
        const ImWchar* symbolRanges, const ImWchar* cjkRanges,
        const ImWchar* hangulRanges, const ImWchar* emojiRanges) {
        MergeFont(destination, "C:\\Windows\\Fonts\\seguisym.ttf",
            size, symbolRanges);
        MergeFont(destination, "C:\\Windows\\Fonts\\msyh.ttc",
            size, cjkRanges);
        MergeFont(destination, "C:\\Windows\\Fonts\\malgun.ttf",
            size, hangulRanges);
        MergeFont(destination, "C:\\Windows\\Fonts\\seguiemj.ttf",
            size, emojiRanges);
    }
}

bool InitializeTextFonts() {
    ImGuiIO& io = ImGui::GetIO();

    // 1. Classic Retro font (ProggyClean default built-in to ImGui)
    g_classicFont = io.Fonts->AddFontDefault();

    // 2. Modern font (Segoe UI)
    if (FileExists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        g_modernPlayerFont = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\segoeui.ttf", kPlayerTextSize);
        g_modernUiFont = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\segoeui.ttf", kUiTextSize);
    }
    if (!g_modernPlayerFont) g_modernPlayerFont = g_classicFont;
    if (!g_modernUiFont) g_modernUiFont = g_classicFont;

    static const ImWchar symbolRanges[] = {
        0x2000, 0x2BFF,
        0
    };
    static const ImWchar cjkRanges[] = {
        0x2E80, 0x9FFF,
        0xF900, 0xFAFF,
        0xFF00, 0xFFEF,
        0
    };
    static const ImWchar hangulRanges[] = {
        0x1100, 0x11FF,
        0x3130, 0x318F,
        0xAC00, 0xD7AF,
        0
    };
    static const ImWchar emojiRanges[] = {
        0x1F000, 0x1FAFF,
        0
    };

    if (g_modernPlayerFont && g_modernPlayerFont != g_classicFont) {
        MergeFallbacks(g_modernPlayerFont, kPlayerTextSize,
            symbolRanges, cjkRanges, hangulRanges, emojiRanges);
    }
    if (g_modernUiFont && g_modernUiFont != g_classicFont && g_modernUiFont != g_modernPlayerFont) {
        MergeFallbacks(g_modernUiFont, kUiTextSize,
            symbolRanges, cjkRanges, hangulRanges, emojiRanges);
    }

    io.FontDefault = (g_Esp.fontMode == 1 && g_classicFont) ? g_classicFont : g_modernPlayerFont;
    return true;
}

ImFont* GetPlayerNameFont() {
    if (g_Esp.fontMode == 1 && g_classicFont)
        return g_classicFont;
    return g_modernPlayerFont ? g_modernPlayerFont : g_classicFont;
}

ImFont* GetUiFont() {
    if (g_Esp.fontMode == 1 && g_classicFont)
        return g_classicFont;
    return g_modernUiFont ? g_modernUiFont : g_classicFont;
}

ImFont* GetClassicFont() {
    return g_classicFont;
}
