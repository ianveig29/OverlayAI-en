#pragma once

#include "Types.h"
#include "imgui.h"

ImU32 EspColor(int r, int g, int b, int a = 255);
void DrawOutlinedText(ImDrawList* drawList, const ImVec2& pos, ImU32 color, const char* text);
ImU32 GetVisibilityColor(bool visible, bool useVisibility,
    int visR, int visG, int visB, int hidR, int hidG, int hidB,
    int baseR, int baseG, int baseB);
ImVec2 CalcAnchoredTextPos(int anchor, float boxX, float boxY, float boxW, float boxH, const ImVec2& textSize);
void DrawAnchoredOutlinedText(ImDrawList* drawList, int anchor, float boxX, float boxY, float boxW, float boxH,
    ImU32 color, const char* text);
void DrawEspBox(ImDrawList* drawList, float x, float y, float w, float h, ImU32 color, float outlineThickness);
