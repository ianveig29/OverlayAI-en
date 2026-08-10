// ============================================================
// Draw.cpp
// Basic drawing functions: boxes, lines, text, circles. Used by ESP and other modules to draw on screen.
// ============================================================

#include "Draw.h"
#include "../imgui.h"


ImU32 EspColor(int r, int g, int b, int a) {
    return IM_COL32((ImU32)r, (ImU32)g, (ImU32)b, (ImU32)a);
}

void DrawOutlinedText(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text) {
    ImVec2 sz = ImGui::CalcTextSize(text);
    drawList->AddRectFilled(
        ImVec2(pos.x - 2.0f, pos.y - 1.0f),
        ImVec2(pos.x + sz.x + 2.0f, pos.y + sz.y + 1.0f),
        IM_COL32(0, 0, 0, 170));
    drawList->AddText(pos, color, text);
}

ImU32 GetVisibilityColor(bool visible, bool useVisibility,
    int visR, int visG, int visB, int hidR, int hidG, int hidB,
    int baseR, int baseG, int baseB)
{
    if (useVisibility)
        return visible ? EspColor(visR, visG, visB) : EspColor(hidR, hidG, hidB);
    return EspColor(baseR, baseG, baseB);
}

ImVec2 CalcAnchoredTextPos(int anchor, float boxX, float boxY, float boxW, float boxH, const ImVec2& textSize) {
    ImVec2 pos{};
    switch (anchor) {
    case EspTextBottom:
        pos.x = boxX + boxW * 0.5f - textSize.x * 0.5f;
        pos.y = boxY + boxH + 4.0f;
        break;
    case EspTextLeft:
        pos.x = boxX - textSize.x - 8.0f;
        pos.y = boxY + boxH * 0.5f - textSize.y * 0.5f;
        break;
    case EspTextRight:
        pos.x = boxX + boxW + 8.0f;
        pos.y = boxY + boxH * 0.5f - textSize.y * 0.5f;
        break;
    case EspTextTop:
    default:
        pos.x = boxX + boxW * 0.5f - textSize.x * 0.5f;
        pos.y = boxY - textSize.y - 4.0f;
        break;
    }
    return pos;
}

void DrawAnchoredOutlinedText(ImDrawList* drawList, int anchor, float boxX, float boxY, float boxW, float boxH,
    ImU32 color, const char* text)
{
    ImVec2 sz = ImGui::CalcTextSize(text);
    ImVec2 pos = CalcAnchoredTextPos(anchor, boxX, boxY, boxW, boxH, sz);
    DrawOutlinedText(drawList, pos, color, text);
}

void DrawEspBox(ImDrawList* drawList, float x, float y, float w, float h, ImU32 color, float outlineThickness) {
    constexpr float innerThickness = 1.5f;
    if (outlineThickness > 0.0f) {
        drawList->AddRect(
            ImVec2(x - outlineThickness, y - outlineThickness),
            ImVec2(x + w + outlineThickness, y + h + outlineThickness),
            IM_COL32(0, 0, 0, 255), 0.0f, 0, outlineThickness);
    }
    drawList->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color, 0.0f, 0, innerThickness);
}
