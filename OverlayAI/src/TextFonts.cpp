// ============================================================
// TextFonts.cpp
// Loads and configures text fonts used for drawing on screen.
// ============================================================

#include "TextFonts.h"

#include "../imgui.h"

#include <Windows.h>

namespace {
    constexpr float kPlayerTextSize = 13.0f;
    constexpr float kUiTextSize = 16.0f;
    ImFont* g_playerNameFont = nullptr;
    ImFont* g_uiFont = nullptr;

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
    if (FileExists("C:\\Windows\\Fonts\\segoeui.ttf")) {
        g_playerNameFont = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\segoeui.ttf", kPlayerTextSize);
        g_uiFont = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\segoeui.ttf", kUiTextSize);
    }
    if (!g_playerNameFont)
        g_playerNameFont = io.Fonts->AddFontDefault();
    if (!g_uiFont)
        g_uiFont = g_playerNameFont;
    if (!g_playerNameFont) return false;

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

    MergeFallbacks(g_playerNameFont, kPlayerTextSize,
        symbolRanges, cjkRanges, hangulRanges, emojiRanges);
    if (g_uiFont != g_playerNameFont)
        MergeFallbacks(g_uiFont, kUiTextSize,
            symbolRanges, cjkRanges, hangulRanges, emojiRanges);
    io.FontDefault = g_playerNameFont;
    return true;
}

ImFont* GetPlayerNameFont() {
    return g_playerNameFont;
}

ImFont* GetUiFont() {
    return g_uiFont ? g_uiFont : g_playerNameFont;
}
