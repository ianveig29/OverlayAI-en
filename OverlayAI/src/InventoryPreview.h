#pragma once

// ============================================================
// InventoryPreview.h
// Inventory preview function declarations.
// ============================================================

struct ID3D11ShaderResourceView;

enum class InventoryPreviewState {
    Idle,
    Loading,
    Ready,
    Failed
};

struct InventoryPreviewInfo {
    InventoryPreviewState state = InventoryPreviewState::Idle;
    int catalogIndex = -1;
    int width = 0;
    int height = 0;
    const char* error = "";
};

void RequestInventoryPreview(int catalogIndex, const char* imageUrl);
ID3D11ShaderResourceView* GetInventoryPreviewTexture();
InventoryPreviewInfo GetInventoryPreviewInfo();
void RetryInventoryPreview();
void ShutdownInventoryPreview();
