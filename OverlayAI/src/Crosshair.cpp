#include "Crosshair.h"

#include "Config.h"
#include "imgui.h"

void RenderCrosshair(int screenWidth, int screenHeight) {
    if (!g_Esp.showCrosshair || screenWidth <= 0 || screenHeight <= 0) return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImVec2 center(screenWidth * 0.5f, screenHeight * 0.5f);
    constexpr float gap = 3.0f;
    constexpr float length = 7.0f;
    constexpr float outlineThickness = 3.0f;
    constexpr float lineThickness = 1.25f;
    const ImU32 outline = IM_COL32(0, 0, 0, 210);
    const ImU32 foreground = IM_COL32(235, 245, 240, 245);

    const ImVec2 segments[][2] = {
        { { center.x - gap - length, center.y }, { center.x - gap, center.y } },
        { { center.x + gap, center.y }, { center.x + gap + length, center.y } },
        { { center.x, center.y - gap - length }, { center.x, center.y - gap } },
        { { center.x, center.y + gap }, { center.x, center.y + gap + length } }
    };
    for (const auto& segment : segments)
        drawList->AddLine(segment[0], segment[1], outline, outlineThickness);
    for (const auto& segment : segments)
        drawList->AddLine(segment[0], segment[1], foreground, lineThickness);
    drawList->AddCircleFilled(center, 1.0f, foreground, 8);
}
