#include "OtherGlow.h"
#include "WorldTransform.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <d3dcompiler.h>
#include <vector>

namespace {
    struct MaskVertex {
        float x;
        float y;
        float r;
        float g;
        float b;
        float a;
    };

    struct BlurConstants {
        float directionX;
        float directionY;
        float padding[2];
    };

    ID3D11Device* g_device = nullptr;
    ID3D11DeviceContext* g_context = nullptr;
    ID3D11VertexShader* g_maskVs = nullptr;
    ID3D11PixelShader* g_maskPs = nullptr;
    ID3D11VertexShader* g_fullscreenVs = nullptr;
    ID3D11PixelShader* g_blurPs = nullptr;
    ID3D11PixelShader* g_compositePs = nullptr;
    ID3D11InputLayout* g_inputLayout = nullptr;
    ID3D11Buffer* g_vertexBuffer = nullptr;
    ID3D11Buffer* g_blurConstants = nullptr;
    ID3D11SamplerState* g_sampler = nullptr;
    ID3D11RasterizerState* g_maskRasterizer = nullptr;
    ID3D11BlendState* g_compositeBlend = nullptr;
    ID3D11Texture2D* g_maskTexture = nullptr;
    ID3D11RenderTargetView* g_maskRtv = nullptr;
    ID3D11ShaderResourceView* g_maskSrv = nullptr;
    ID3D11Texture2D* g_blurTextureA = nullptr;
    ID3D11RenderTargetView* g_blurRtvA = nullptr;
    ID3D11ShaderResourceView* g_blurSrvA = nullptr;
    ID3D11Texture2D* g_blurTextureB = nullptr;
    ID3D11RenderTargetView* g_blurRtvB = nullptr;
    ID3D11ShaderResourceView* g_blurSrvB = nullptr;
    std::vector<MaskVertex> g_vertices;
    int g_screenWidth = 0;
    int g_screenHeight = 0;
    int g_textureWidth = 0;
    int g_textureHeight = 0;
    int g_blurTextureWidth = 0;
    int g_blurTextureHeight = 0;
    size_t g_vertexCapacity = 0;
    float g_contentMinX = FLT_MAX;
    float g_contentMinY = FLT_MAX;
    float g_contentMaxX = -FLT_MAX;
    float g_contentMaxY = -FLT_MAX;

    constexpr char kMaskShader[] = R"(
struct VertexInput { float2 position : POSITION; float4 color : COLOR0; };
struct VertexOutput { float4 position : SV_POSITION; float4 color : COLOR0; };
VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}
float4 PSMain(VertexOutput input) : SV_TARGET { return input.color; }
)";

    constexpr char kFullscreenShader[] = R"(
struct VertexOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VertexOutput VSMain(uint id : SV_VertexID) {
    VertexOutput output;
    float2 position = float2((id << 1) & 2, id & 2);
    output.uv = position;
    output.position = float4(position * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
Texture2D sourceTexture : register(t0);
Texture2D maskTexture : register(t1);
SamplerState linearSampler : register(s0);
cbuffer BlurConstants : register(b0) { float2 sampleDirection; float2 padding; };
float4 BlurPS(VertexOutput input) : SV_TARGET {
    float4 color = sourceTexture.Sample(linearSampler, input.uv) * 0.227027;
    color += sourceTexture.Sample(linearSampler, input.uv + sampleDirection * 1.384615) * 0.316216;
    color += sourceTexture.Sample(linearSampler, input.uv - sampleDirection * 1.384615) * 0.316216;
    color += sourceTexture.Sample(linearSampler, input.uv + sampleDirection * 3.230769) * 0.070270;
    color += sourceTexture.Sample(linearSampler, input.uv - sampleDirection * 3.230769) * 0.070270;
    return color;
}
float4 CompositePS(VertexOutput input) : SV_TARGET {
    float4 blurred = sourceTexture.Sample(linearSampler, input.uv);
    float4 mask = maskTexture.Sample(linearSampler, input.uv);
    float coverage = smoothstep(0.002, 0.075, mask.a);
    float haloAlpha = blurred.a * (1.0 - coverage) * 2.15;
    float coreAlpha = mask.a * 0.12;
    float alpha = saturate(haloAlpha + coreAlpha);
    float3 haloColor = blurred.a > 0.001 ? blurred.rgb / blurred.a : 0.0;
    float3 coreColor = mask.a > 0.001 ? mask.rgb / mask.a : haloColor;
    return float4(lerp(haloColor, coreColor, coverage), alpha);
}
)";

    template <typename T>
    void ReleaseResource(T*& resource) {
        if (resource) {
            resource->Release();
            resource = nullptr;
        }
    }

    void ReleaseTextures() {
        ReleaseResource(g_maskSrv);
        ReleaseResource(g_maskRtv);
        ReleaseResource(g_maskTexture);
        ReleaseResource(g_blurSrvA);
        ReleaseResource(g_blurRtvA);
        ReleaseResource(g_blurTextureA);
        ReleaseResource(g_blurSrvB);
        ReleaseResource(g_blurRtvB);
        ReleaseResource(g_blurTextureB);
        g_textureWidth = 0;
        g_textureHeight = 0;
        g_blurTextureWidth = 0;
        g_blurTextureHeight = 0;
    }

    bool CompileShader(const char* source, const char* entry, const char* target, ID3DBlob** blob) {
        ID3DBlob* errors = nullptr;
        const HRESULT result = D3DCompile(source, strlen(source), nullptr, nullptr, nullptr,
            entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, &errors);
        ReleaseResource(errors);
        return SUCCEEDED(result);
    }

    bool CreateGlowTexture(int width, int height, ID3D11Texture2D** texture,
        ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(g_device->CreateTexture2D(&desc, nullptr, texture)) &&
            SUCCEEDED(g_device->CreateRenderTargetView(*texture, nullptr, rtv)) &&
            SUCCEEDED(g_device->CreateShaderResourceView(*texture, nullptr, srv));
    }

    bool EnsureTextures() {
        const int width = (std::max)(1, g_screenWidth);
        const int height = (std::max)(1, g_screenHeight);
        const int blurWidth = (std::max)(1, (width + 1) / 2);
        const int blurHeight = (std::max)(1, (height + 1) / 2);
        if (width == g_textureWidth && height == g_textureHeight &&
            blurWidth == g_blurTextureWidth && blurHeight == g_blurTextureHeight &&
            g_maskTexture && g_blurTextureA && g_blurTextureB)
            return true;
        ReleaseTextures();
        g_textureWidth = width;
        g_textureHeight = height;
        g_blurTextureWidth = blurWidth;
        g_blurTextureHeight = blurHeight;
        if (!CreateGlowTexture(g_textureWidth, g_textureHeight,
                &g_maskTexture, &g_maskRtv, &g_maskSrv) ||
            !CreateGlowTexture(g_blurTextureWidth, g_blurTextureHeight,
                &g_blurTextureA, &g_blurRtvA, &g_blurSrvA) ||
            !CreateGlowTexture(g_blurTextureWidth, g_blurTextureHeight,
                &g_blurTextureB, &g_blurRtvB, &g_blurSrvB)) {
            ReleaseTextures();
            return false;
        }
        return true;
    }

    MaskVertex MakeVertex(const ImVec2& point, const ImVec4& color) {
        return {
            point.x / static_cast<float>(g_screenWidth) * 2.0f - 1.0f,
            1.0f - point.y / static_cast<float>(g_screenHeight) * 2.0f,
            color.x, color.y, color.z, color.w
        };
    }

    void AddTriangle(const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec4& color) {
        g_vertices.push_back(MakeVertex(a, color));
        g_vertices.push_back(MakeVertex(b, color));
        g_vertices.push_back(MakeVertex(c, color));
    }

    void AddEllipse(const ImVec2& center, float radiusX, float radiusY, float rotation,
        const ImVec4& color, int segments = 20) {
        const float cosine = std::cos(rotation);
        const float sine = std::sin(rotation);
        auto pointAt = [&](float angle) {
            const float x = std::cos(angle) * radiusX;
            const float y = std::sin(angle) * radiusY;
            return ImVec2(center.x + x * cosine - y * sine, center.y + x * sine + y * cosine);
        };
        ImVec2 previous = pointAt(0.0f);
        for (int index = 1; index <= segments; ++index) {
            const float angle = 6.28318530718f * static_cast<float>(index) / static_cast<float>(segments);
            const ImVec2 current = pointAt(angle);
            AddTriangle(center, previous, current, color);
            previous = current;
        }
    }

    void AddCapsule(const ImVec2& a, const ImVec2& b, float width, const ImVec4& color) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.5f) return;
        const float radius = width * 0.5f;
        const ImVec2 perpendicular(-dy / length * radius, dx / length * radius);
        AddTriangle(ImVec2(a.x + perpendicular.x, a.y + perpendicular.y),
            ImVec2(b.x + perpendicular.x, b.y + perpendicular.y),
            ImVec2(b.x - perpendicular.x, b.y - perpendicular.y), color);
        AddTriangle(ImVec2(a.x + perpendicular.x, a.y + perpendicular.y),
            ImVec2(b.x - perpendicular.x, b.y - perpendicular.y),
            ImVec2(a.x - perpendicular.x, a.y - perpendicular.y), color);
        AddEllipse(a, radius, radius, 0.0f, color, 16);
        AddEllipse(b, radius, radius, 0.0f, color, 16);
    }

    void AddTaperedSegment(const ImVec2& a, const ImVec2& b, float widthA, float widthB,
        const ImVec4& color) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.5f) return;
        const ImVec2 normal(-dy / length, dx / length);
        const ImVec2 aOffset(normal.x * widthA * 0.5f, normal.y * widthA * 0.5f);
        const ImVec2 bOffset(normal.x * widthB * 0.5f, normal.y * widthB * 0.5f);
        const ImVec2 aLeft(a.x + aOffset.x, a.y + aOffset.y);
        const ImVec2 aRight(a.x - aOffset.x, a.y - aOffset.y);
        const ImVec2 bLeft(b.x + bOffset.x, b.y + bOffset.y);
        const ImVec2 bRight(b.x - bOffset.x, b.y - bOffset.y);
        AddTriangle(aLeft, bLeft, bRight, color);
        AddTriangle(aLeft, bRight, aRight, color);
    }

    bool EnsureVertexBuffer() {
        if (g_vertices.size() <= g_vertexCapacity && g_vertexBuffer) return true;
        ReleaseResource(g_vertexBuffer);
        g_vertexCapacity = (std::max)(g_vertices.size(), static_cast<size_t>(4096));
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(g_vertexCapacity * sizeof(MaskVertex));
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(g_device->CreateBuffer(&desc, nullptr, &g_vertexBuffer));
    }

    void DrawFullscreen(ID3D11ShaderResourceView* source, ID3D11RenderTargetView* target,
        ID3D11PixelShader* pixelShader, float directionX, float directionY) {
        ID3D11ShaderResourceView* nullSrv = nullptr;
        g_context->PSSetShaderResources(0, 1, &nullSrv);
        g_context->OMSetRenderTargets(1, &target, nullptr);
        BlurConstants constants{ directionX, directionY, {} };
        g_context->UpdateSubresource(g_blurConstants, 0, nullptr, &constants, 0, 0);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_fullscreenVs, nullptr, 0);
        g_context->PSSetShader(pixelShader, nullptr, 0);
        g_context->PSSetConstantBuffers(0, 1, &g_blurConstants);
        g_context->PSSetSamplers(0, 1, &g_sampler);
        g_context->PSSetShaderResources(0, 1, &source);
        g_context->Draw(3, 0);
        g_context->PSSetShaderResources(0, 1, &nullSrv);
    }

    void DrawComposite(ID3D11ShaderResourceView* blurred, ID3D11ShaderResourceView* mask,
        ID3D11RenderTargetView* target) {
        ID3D11ShaderResourceView* nullSrvs[2] = {};
        g_context->PSSetShaderResources(0, 2, nullSrvs);
        g_context->OMSetRenderTargets(1, &target, nullptr);
        g_context->IASetInputLayout(nullptr);
        g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_context->VSSetShader(g_fullscreenVs, nullptr, 0);
        g_context->PSSetShader(g_compositePs, nullptr, 0);
        g_context->PSSetSamplers(0, 1, &g_sampler);
        ID3D11ShaderResourceView* sources[2] = { blurred, mask };
        g_context->PSSetShaderResources(0, 2, sources);
        g_context->Draw(3, 0);
        g_context->PSSetShaderResources(0, 2, nullSrvs);
    }
}

bool InitializeOtherGlow(ID3D11Device* device, ID3D11DeviceContext* context) {
    if (!device || !context) return false;
    g_device = device;
    g_context = context;

    ID3DBlob* maskVsBlob = nullptr;
    ID3DBlob* maskPsBlob = nullptr;
    ID3DBlob* fullscreenVsBlob = nullptr;
    ID3DBlob* blurPsBlob = nullptr;
    ID3DBlob* compositePsBlob = nullptr;
    const bool compiled = CompileShader(kMaskShader, "VSMain", "vs_4_0", &maskVsBlob) &&
        CompileShader(kMaskShader, "PSMain", "ps_4_0", &maskPsBlob) &&
        CompileShader(kFullscreenShader, "VSMain", "vs_4_0", &fullscreenVsBlob) &&
        CompileShader(kFullscreenShader, "BlurPS", "ps_4_0", &blurPsBlob) &&
        CompileShader(kFullscreenShader, "CompositePS", "ps_4_0", &compositePsBlob);
    if (!compiled) {
        ReleaseResource(maskVsBlob);
        ReleaseResource(maskPsBlob);
        ReleaseResource(fullscreenVsBlob);
        ReleaseResource(blurPsBlob);
        ReleaseResource(compositePsBlob);
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    bool success = SUCCEEDED(device->CreateVertexShader(maskVsBlob->GetBufferPointer(), maskVsBlob->GetBufferSize(), nullptr, &g_maskVs)) &&
        SUCCEEDED(device->CreatePixelShader(maskPsBlob->GetBufferPointer(), maskPsBlob->GetBufferSize(), nullptr, &g_maskPs)) &&
        SUCCEEDED(device->CreateInputLayout(inputElements, 2, maskVsBlob->GetBufferPointer(), maskVsBlob->GetBufferSize(), &g_inputLayout)) &&
        SUCCEEDED(device->CreateVertexShader(fullscreenVsBlob->GetBufferPointer(), fullscreenVsBlob->GetBufferSize(), nullptr, &g_fullscreenVs)) &&
        SUCCEEDED(device->CreatePixelShader(blurPsBlob->GetBufferPointer(), blurPsBlob->GetBufferSize(), nullptr, &g_blurPs)) &&
        SUCCEEDED(device->CreatePixelShader(compositePsBlob->GetBufferPointer(), compositePsBlob->GetBufferSize(), nullptr, &g_compositePs));
    ReleaseResource(maskVsBlob);
    ReleaseResource(maskPsBlob);
    ReleaseResource(fullscreenVsBlob);
    ReleaseResource(blurPsBlob);
    ReleaseResource(compositePsBlob);
    if (!success) {
        ShutdownOtherGlow();
        return false;
    }

    D3D11_BUFFER_DESC constantsDesc{};
    constantsDesc.ByteWidth = sizeof(BlurConstants);
    constantsDesc.Usage = D3D11_USAGE_DEFAULT;
    constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    success = SUCCEEDED(device->CreateBuffer(&constantsDesc, nullptr, &g_blurConstants));

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    success = success && SUCCEEDED(device->CreateSamplerState(&samplerDesc, &g_sampler));

    D3D11_BLEND_DESC blendDesc{};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    success = success && SUCCEEDED(device->CreateBlendState(&blendDesc, &g_compositeBlend));

    D3D11_RASTERIZER_DESC rasterizerDesc{};
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.ScissorEnable = TRUE;
    success = success && SUCCEEDED(device->CreateRasterizerState(
        &rasterizerDesc, &g_maskRasterizer));
    if (!success) ShutdownOtherGlow();
    return success;
}

void ShutdownOtherGlow() {
    ReleaseTextures();
    ReleaseResource(g_maskRasterizer);
    ReleaseResource(g_compositeBlend);
    ReleaseResource(g_sampler);
    ReleaseResource(g_blurConstants);
    ReleaseResource(g_vertexBuffer);
    ReleaseResource(g_inputLayout);
    ReleaseResource(g_compositePs);
    ReleaseResource(g_blurPs);
    ReleaseResource(g_fullscreenVs);
    ReleaseResource(g_maskPs);
    ReleaseResource(g_maskVs);
    g_vertices.clear();
    g_vertexCapacity = 0;
    g_device = nullptr;
    g_context = nullptr;
}

void ReleaseOtherGlowRenderTargets() {
    ReleaseTextures();
    g_vertices.clear();
}

void BeginOtherGlowFrame(int screenWidth, int screenHeight) {
    g_screenWidth = screenWidth;
    g_screenHeight = screenHeight;
    g_vertices.clear();
    g_contentMinX = FLT_MAX;
    g_contentMinY = FLT_MAX;
    g_contentMaxX = -FLT_MAX;
    g_contentMaxY = -FLT_MAX;
}

void SubmitOtherGlowPose(const SkeletonPose& pose, const Matrix4x4& viewMatrix,
    int screenWidth, int screenHeight, ImU32 color, float bodyHeight, float bodyScale) {
    if (!g_device || bodyHeight < 8.0f || screenWidth <= 0 || screenHeight <= 0) return;
    std::array<ImVec2, kSkeletonBoneCount> bones{};
    std::array<bool, kSkeletonBoneCount> projected{};
    for (size_t index = 0; index < kSkeletonBoneCount; ++index) {
        if (!pose.valid[index]) continue;
        Vector3 screen{};
        float clipW = 0.0f;
        const bool inRegularRange = WorldToScreen(pose.bones[index], screen, viewMatrix,
            screenWidth, screenHeight, nullptr, &clipW);
        if (!inRegularRange && (!std::isfinite(clipW) || clipW <= 0.01f)) continue;
        if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) continue;

        // Keep partially visible limbs when a nearby model extends beyond the
        // regular W2S guard, while bounding coordinates for stable rasterization.
        screen.x = std::clamp(screen.x, -screenWidth * 2.0f, screenWidth * 3.0f);
        screen.y = std::clamp(screen.y, -screenHeight * 2.0f, screenHeight * 3.0f);
        bones[index] = ImVec2(screen.x, screen.y);
        projected[index] = true;
    }

    ImVec4 maskColor = ImGui::ColorConvertU32ToFloat4(color);
    maskColor.x *= maskColor.w;
    maskColor.y *= maskColor.w;
    maskColor.z *= maskColor.w;
    const float scale = std::clamp(bodyScale, 0.55f, 2.0f);
    const float boundsPadding = bodyHeight * 0.16f * scale;
    for (size_t index = 0; index < kSkeletonBoneCount; ++index) {
        if (!projected[index]) continue;
        g_contentMinX = (std::min)(g_contentMinX, bones[index].x - boundsPadding);
        g_contentMinY = (std::min)(g_contentMinY, bones[index].y - boundsPadding);
        g_contentMaxX = (std::max)(g_contentMaxX, bones[index].x + boundsPadding);
        g_contentMaxY = (std::max)(g_contentMaxY, bones[index].y + boundsPadding);
    }
    struct Segment { int from; int to; float widthFrom; float widthTo; };
    constexpr Segment segments[] = {
        { 6, 7, 0.052f, 0.060f },
        { 5, 8, 0.090f, 0.075f }, { 8, 9, 0.075f, 0.062f },
        { 9, 10, 0.062f, 0.052f }, { 10, 11, 0.052f, 0.040f },
        { 5, 12, 0.090f, 0.075f }, { 12, 13, 0.075f, 0.062f },
        { 13, 14, 0.062f, 0.052f }, { 14, 15, 0.052f, 0.040f },
        { 1, 17, 0.115f, 0.105f }, { 17, 18, 0.105f, 0.080f },
        { 18, 19, 0.080f, 0.052f },
        { 1, 20, 0.115f, 0.105f }, { 20, 21, 0.105f, 0.080f },
        { 21, 22, 0.080f, 0.052f }
    };
    for (const Segment& segment : segments) {
        if (projected[segment.from] && projected[segment.to])
            AddTaperedSegment(bones[segment.from], bones[segment.to],
                (std::max)(2.0f, bodyHeight * segment.widthFrom * scale),
                (std::max)(2.0f, bodyHeight * segment.widthTo * scale), maskColor);
    }

    // Overlapping torso volumes soften the otherwise angular skeleton hull.
    if (projected[5] && projected[1]) {
        const float dx = bones[1].x - bones[5].x;
        const float dy = bones[1].y - bones[5].y;
        const float torsoLength = std::sqrt(dx * dx + dy * dy);
        const float rotation = std::atan2(dy, dx) - 1.57079632679f;
        const ImVec2 chestCenter(bones[5].x + dx * 0.34f, bones[5].y + dy * 0.34f);
        const ImVec2 abdomenCenter(bones[5].x + dx * 0.72f, bones[5].y + dy * 0.72f);
        AddEllipse(chestCenter, bodyHeight * 0.112f * scale,
            torsoLength * 0.38f, rotation, maskColor, 24);
        AddEllipse(abdomenCenter, bodyHeight * 0.086f * scale,
            torsoLength * 0.32f, rotation, maskColor, 22);
    }

    struct RoundedJoint { int bone; float radius; };
    constexpr RoundedJoint roundedJoints[] = {
        { 8, 0.040f }, { 9, 0.032f }, { 10, 0.025f },
        { 12, 0.040f }, { 13, 0.032f }, { 14, 0.025f },
        { 17, 0.052f }, { 18, 0.038f },
        { 20, 0.052f }, { 21, 0.038f }
    };
    for (const RoundedJoint& joint : roundedJoints) {
        if (!projected[joint.bone]) continue;
        const float radius = bodyHeight * joint.radius * scale;
        AddEllipse(bones[joint.bone], radius, radius, 0.0f, maskColor, 16);
    }

    if (projected[8] && projected[12] && projected[17] && projected[20]) {
        const ImVec2 shoulderCenter((bones[8].x + bones[12].x) * 0.5f,
            (bones[8].y + bones[12].y) * 0.5f);
        const ImVec2 hipCenter((bones[17].x + bones[20].x) * 0.5f,
            (bones[17].y + bones[20].y) * 0.5f);
        const float shoulderScale = scale * 1.08f;
        const ImVec2 leftShoulder(shoulderCenter.x + (bones[8].x - shoulderCenter.x) * shoulderScale,
            shoulderCenter.y + (bones[8].y - shoulderCenter.y) * shoulderScale);
        const ImVec2 rightShoulder(shoulderCenter.x + (bones[12].x - shoulderCenter.x) * shoulderScale,
            shoulderCenter.y + (bones[12].y - shoulderCenter.y) * shoulderScale);
        const ImVec2 leftHip(hipCenter.x + (bones[17].x - hipCenter.x) * scale,
            hipCenter.y + (bones[17].y - hipCenter.y) * scale);
        const ImVec2 rightHip(hipCenter.x + (bones[20].x - hipCenter.x) * scale,
            hipCenter.y + (bones[20].y - hipCenter.y) * scale);
        AddTriangle(leftShoulder, rightShoulder, rightHip, maskColor);
        AddTriangle(leftShoulder, rightHip, leftHip, maskColor);
        const float shoulderDx = rightShoulder.x - leftShoulder.x;
        const float shoulderDy = rightShoulder.y - leftShoulder.y;
        const float shoulderRadius = std::sqrt(shoulderDx * shoulderDx + shoulderDy * shoulderDy) * 0.54f;
        AddEllipse(shoulderCenter, shoulderRadius, bodyHeight * 0.100f * scale,
            std::atan2(shoulderDy, shoulderDx), maskColor, 24);

        const float hipDx = rightHip.x - leftHip.x;
        const float hipDy = rightHip.y - leftHip.y;
        const float hipRadius = std::sqrt(hipDx * hipDx + hipDy * hipDy) * 0.54f;
        AddEllipse(hipCenter, hipRadius, bodyHeight * 0.090f * scale,
            std::atan2(hipDy, hipDx), maskColor, 22);
    }
    if (projected[7]) {
        float rotation = 0.0f;
        if (projected[6])
            rotation = std::atan2(bones[7].y - bones[6].y, bones[7].x - bones[6].x) - 1.57079632679f;
        AddEllipse(bones[7], bodyHeight * 0.055f * scale,
            bodyHeight * 0.072f * scale, rotation, maskColor, 24);
    }

    constexpr std::array<std::array<int, 2>, 2> hands = { {
        { 10, 11 }, { 14, 15 }
    } };
    for (const auto& hand : hands) {
        if (!projected[hand[0]] || !projected[hand[1]]) continue;
        const float dx = bones[hand[1]].x - bones[hand[0]].x;
        const float dy = bones[hand[1]].y - bones[hand[0]].y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.5f) continue;
        const float extension = bodyHeight * 0.018f * scale;
        const ImVec2 center(bones[hand[1]].x + dx / length * extension,
            bones[hand[1]].y + dy / length * extension);
        AddEllipse(center, bodyHeight * 0.052f * scale,
            bodyHeight * 0.032f * scale, std::atan2(dy, dx), maskColor, 16);
    }
    constexpr std::array<std::array<int, 2>, 2> feet = { {
        { 18, 19 }, { 21, 22 }
    } };
    for (const auto& foot : feet) {
        if (!projected[foot[0]] || !projected[foot[1]]) continue;
        float centerX = bones[foot[1]].x;
        if (projected[1])
            centerX += (bones[1].x - centerX) * 0.12f;
        const ImVec2 center(centerX,
            bones[foot[1]].y + bodyHeight * 0.005f * scale);
        AddEllipse(center, bodyHeight * 0.052f * scale,
            bodyHeight * 0.021f * scale, 0.0f, maskColor, 20);
    }
}

void RenderOtherGlow(ID3D11RenderTargetView* target, float softness, int qualityPasses) {
    if (!target || !g_device || !g_context || g_vertices.empty() || !EnsureTextures() || !EnsureVertexBuffer()) return;
    if (g_contentMinX == FLT_MAX || g_contentMinY == FLT_MAX) return;

    const int passes = std::clamp(qualityPasses, 1, 3);
    const float safeSoftness = std::clamp(softness, 2.0f, 20.0f);
    const float haloPadding = safeSoftness * (passes + 1) * 1.75f + 4.0f;
    D3D11_RECT contentRect{
        static_cast<LONG>(std::floor(std::clamp(g_contentMinX - haloPadding, 0.0f,
            static_cast<float>(g_textureWidth)))),
        static_cast<LONG>(std::floor(std::clamp(g_contentMinY - haloPadding, 0.0f,
            static_cast<float>(g_textureHeight)))),
        static_cast<LONG>(std::ceil(std::clamp(g_contentMaxX + haloPadding, 0.0f,
            static_cast<float>(g_textureWidth)))),
        static_cast<LONG>(std::ceil(std::clamp(g_contentMaxY + haloPadding, 0.0f,
            static_cast<float>(g_textureHeight))))
    };
    if (contentRect.right <= contentRect.left || contentRect.bottom <= contentRect.top) return;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_context->Map(g_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, g_vertices.data(), g_vertices.size() * sizeof(MaskVertex));
    g_context->Unmap(g_vertexBuffer, 0);

    const float clear[4] = {};
    g_context->ClearRenderTargetView(g_maskRtv, clear);
    g_context->ClearRenderTargetView(g_blurRtvA, clear);
    g_context->ClearRenderTargetView(g_blurRtvB, clear);
    D3D11_VIEWPORT maskViewport{ 0.0f, 0.0f, static_cast<float>(g_textureWidth),
        static_cast<float>(g_textureHeight), 0.0f, 1.0f };
    g_context->RSSetViewports(1, &maskViewport);
    g_context->RSSetState(g_maskRasterizer);
    g_context->RSSetScissorRects(1, &contentRect);
    g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    g_context->OMSetRenderTargets(1, &g_maskRtv, nullptr);
    const UINT stride = sizeof(MaskVertex);
    const UINT offset = 0;
    g_context->IASetInputLayout(g_inputLayout);
    g_context->IASetVertexBuffers(0, 1, &g_vertexBuffer, &stride, &offset);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_maskVs, nullptr, 0);
    g_context->PSSetShader(g_maskPs, nullptr, 0);
    g_context->Draw(static_cast<UINT>(g_vertices.size()), 0);

    const float radius = safeSoftness * 0.5f;
    D3D11_VIEWPORT blurViewport{ 0.0f, 0.0f, static_cast<float>(g_blurTextureWidth),
        static_cast<float>(g_blurTextureHeight), 0.0f, 1.0f };
    g_context->RSSetViewports(1, &blurViewport);
    D3D11_RECT blurRect{
        contentRect.left * g_blurTextureWidth / g_textureWidth,
        contentRect.top * g_blurTextureHeight / g_textureHeight,
        (contentRect.right * g_blurTextureWidth + g_textureWidth - 1) / g_textureWidth,
        (contentRect.bottom * g_blurTextureHeight + g_textureHeight - 1) / g_textureHeight
    };
    g_context->RSSetScissorRects(1, &blurRect);
    ID3D11ShaderResourceView* source = g_maskSrv;
    for (int pass = 0; pass < passes; ++pass) {
        const float halfResolutionRadius = radius * 0.5f;
        DrawFullscreen(source, g_blurRtvA, g_blurPs,
            halfResolutionRadius / g_blurTextureWidth, 0.0f);
        DrawFullscreen(g_blurSrvA, g_blurRtvB, g_blurPs,
            0.0f, halfResolutionRadius / g_blurTextureHeight);
        source = g_blurSrvB;
    }

    D3D11_VIEWPORT screenViewport{ 0.0f, 0.0f, static_cast<float>(g_screenWidth),
        static_cast<float>(g_screenHeight), 0.0f, 1.0f };
    g_context->RSSetViewports(1, &screenViewport);
    g_context->RSSetScissorRects(1, &contentRect);
    g_context->OMSetBlendState(g_compositeBlend, nullptr, 0xFFFFFFFF);
    DrawComposite(source, g_maskSrv, target);
    g_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
}
