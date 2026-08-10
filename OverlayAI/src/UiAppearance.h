#pragma once

// ============================================================
// UiAppearance.h
// UI appearance function declarations.
// ============================================================

struct UiAppearanceSettings {
    float accent[3]{ 0.12f, 0.42f, 0.75f };
    float background[3]{ 0.11f, 0.12f, 0.15f };
    float text[3]{ 0.95f, 0.96f, 0.98f };
};

void InitializeUiAppearance();
const UiAppearanceSettings& GetUiAppearance();
void SetUiAppearance(const UiAppearanceSettings& settings);
void ResetUiAppearance();
