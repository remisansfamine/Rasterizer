﻿
#include <cstdio>
#include <cstring>
#include <cassert>

#include <imgui.h>

#include <common/maths.hpp>

#include "renderer_impl.hpp"

#include <algorithm>

#include <bitset>
#include <iostream>

struct clipPoint
{
    float4 coords;
    float3 weights;
};

rdrImpl* rdrInit(float* colorBuffer32Bits, float* depthBuffer, int width, int height)
{
    rdrImpl* renderer = new rdrImpl();

    renderer->fb.colorBuffer = reinterpret_cast<float4*>(colorBuffer32Bits);
    renderer->fb.depthBuffer = depthBuffer;
    renderer->fb.width = width;
    renderer->fb.height = height;

    renderer->viewport = Viewport{ 0, 0, width, height };

    return renderer;
}

float4 gammaCorrection(const float4& color, float iGamma)
{
    return
    {
        powf(color.r, iGamma),
        powf(color.g, iGamma),
        powf(color.b, iGamma),
        color.a
    };
}

float4 boxBlur(Framebuffer fb, int index)
{
    float4 sum =
        fb.colorBuffer[index - 1 + fb.width] + // Top left
        fb.colorBuffer[index     + fb.width] + // Top center
        fb.colorBuffer[index + 1 + fb.width] + // Top right
        fb.colorBuffer[index - 1           ] + // Mid left
        fb.colorBuffer[index               ] + // Current pixel
        fb.colorBuffer[index + 1           ] + // Mid right
        fb.colorBuffer[index - 1 - fb.width] + // Low left
        fb.colorBuffer[index     - fb.width] + // Low center
        fb.colorBuffer[index + 1 - fb.width];  // Low right

    return sum / 9.f;
}

float4 gaussianBlur(Framebuffer fb, int index)
{
    float4 sum =
        fb.colorBuffer[index - 1 + fb.width] + // Top left
        2.f * fb.colorBuffer[index + fb.width] + // Top center
        fb.colorBuffer[index + 1 + fb.width] + // Top right
        2.f * fb.colorBuffer[index - 1] + // Mid left
        4.f * fb.colorBuffer[index] + // Current pixel
        2.f * fb.colorBuffer[index + 1] + // Mid right
        fb.colorBuffer[index - 1 - fb.width] + // Low left
        2.f * fb.colorBuffer[index - fb.width] + // Low center
        fb.colorBuffer[index + 1 - fb.width];  // Low right

    return sum / 16.f;

}

void rdrFinish(rdrImpl* renderer)
{

    for (int i = 0; i < renderer->fb.width; i++)
    {
        for (int j = 0; j < renderer->fb.height; j++)
        {
            int index = i + renderer->fb.width * j;

            float4& color = renderer->fb.colorBuffer[index];

            // Set sum to the average of 9 pixels:
            if (i > 0 && j > 0 && i < renderer->fb.width - 1 && j < renderer->fb.height - 1)
            {
                if (renderer->boxBlur)
                    color = boxBlur(renderer->fb, index);

                if (renderer->gaussianBlur)
                    color = gaussianBlur(renderer->fb, index);
            }
        }
    }

    for (int i = 0; i < renderer->fb.width; i++)
    {
        for (int j = 0; j < renderer->fb.height; j++)
        {
            float4& color = renderer->fb.colorBuffer[i + renderer->fb.width * j];

            color = gammaCorrection(color, renderer->iGamma);
        }
    }
}

void rdrShutdown(rdrImpl* renderer)
{
    delete renderer;
}

void rdrSetUniformFloatV(rdrImpl* renderer, rdrUniformType type, float* value)
{
    switch (type)
    {
        case UT_TIME:      renderer->uniform.time = value[0]; break;
        case UT_DELTATIME: renderer->uniform.deltaTime = value[0]; break;
        case UT_CAMERA_POS: renderer->uniform.cameraPos = float3{ value[0], value[1], value[2] }; break;
        case UT_GLOBALAMBIENT:  renderer->uniform.globalAmbient = float4{ value[0], value[1], value[2], value[3] }; break;
        case UT_GLOBALCOLOR:    renderer->uniform.globalColor = float4{ value[0], value[1], value[2], value[3] }; break;
        default:;
    }
}

void rdrSetUniformBool(rdrImpl* renderer, rdrUniformType type, bool value)
{
    switch (type)
    {
        case UT_DEPTHTEST:      renderer->uniform.depthTest = value; break;
        case UT_STENCTILTEST:   renderer->uniform.stencilTest = value; break;
        default:;
    }
}

void rdrSetUniformLight(rdrImpl* renderer, int index, rdrLight* light)
{
    if (index < 0 || index >= IM_ARRAYSIZE(renderer->uniform.lights))
        return;

    memcpy(&renderer->uniform.lights[index], light, sizeof(rdrLight));
}

void rdrSetProjection(rdrImpl* renderer, float* projectionMatrix)
{
    memcpy(renderer->uniform.projection.e, projectionMatrix, 16 * sizeof(float));
}

void rdrSetView(rdrImpl* renderer, float* viewMatrix)
{
    memcpy(renderer->uniform.view.e, viewMatrix, 16 * sizeof(float));
}

void rdrSetModel(rdrImpl* renderer, float* modelMatrix)
{
    memcpy(renderer->uniform.model.e, modelMatrix, 16 * sizeof(float));
}

void rdrSetViewport(rdrImpl* renderer, int x, int y, int width, int height)
{
    renderer->viewport.x = x;
    renderer->viewport.y = y;
    renderer->viewport.width = width;
    renderer->viewport.height = height;
}

void rdrSetTexture(rdrImpl* renderer, float* colors32Bits, int width, int height)
{
    renderer->uniform.texture =
    {
        width,
        height,
        (float4*)colors32Bits
    };
}

void rdrSetUniformMaterial(rdrImpl* renderer, rdrMaterial* material)
{
    memcpy(&renderer->uniform.material, material, sizeof(rdrMaterial));
}

void drawPixel(float4* colorBuffer, int width, int height, int x, int y, const float4& color)
{
    if (x < 0 || x >= width || y < 0 || y >= height)
        return;

    colorBuffer[y * width + x] = color;
}

void drawLine(float4* colorBuffer, int width, int height, int x0, int y0, int x1, int y1, const float4& color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = (dx > dy ? dx : -dy) / 2, e2;

    for (;;) {
        drawPixel(colorBuffer, width, height, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

void drawLine(const Framebuffer& fb, const float3& p0, const float3& p1, const float4& color)
{
    drawLine(fb.colorBuffer, fb.width, fb.height, (int)roundf(p0.x), (int)roundf(p0.y), (int)roundf(p1.x), (int)roundf(p1.y), color);
}

float3 ndcToScreenCoords(const float3& ndc, const Viewport& viewport)
{
    return
    {
        remap(ndc.x, -1.f, 1.f, viewport.x, viewport.width * 0.5f),
        remap(-ndc.y, -1.f, 1.f, viewport.y, viewport.height * 0.5f),
        remap(-ndc.z, -1.f, 1.f, 0.f, 1.f)
    };
}

float getWeight(const float2& a, const float2& b, const float2& c)
{
    return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

void getLightColor(const Uniform& uniform, Varying& varying)
{
    float4 ambientColorSum = { 0.f, 0.f, 0.f, 0.f };
    float4 diffuseColorSum = { 0.f, 0.f, 0.f, 0.f };
    for (int i = 0; i < IM_ARRAYSIZE(uniform.lights); i++)
    {
        if (!uniform.lights[i].isEnable)
            continue;

        const Light& currLight = uniform.lights[i];

        float3 normal = normalized(varying.normal);
        float3 lightDir = (currLight.lightPos).xyz / (currLight.lightPos).w - currLight.lightPos.w * varying.coords;

        float  distance = magnitude(lightDir);

        lightDir /= distance;

        if (!currLight.lightPos.w)
            lightDir *= -1.f;

        float NdotL = dot(lightDir, normal);

        float attenuation = currLight.constantAttenuation +
                            currLight.linearAttenuation * distance +
                            currLight.quadraticAttenuation * distance * distance;

        // Ambient
        ambientColorSum += currLight.ambient;

        // Diffuse
        diffuseColorSum += max(0.f, NdotL) * currLight.diffuse / attenuation;

        // Specular
        float3 R = normalized(2.f * NdotL * normal - lightDir);
        float3 V = normalized(uniform.cameraPos - varying.coords);

        varying.specularColor += powf(max(0.f, dot(R, V)), uniform.material.shininess) * currLight.specular / attenuation;
    }

    ambientColorSum.a = diffuseColorSum.a = varying.specularColor.a = 0.f;

    varying.shadedColor = uniform.material.ambientColor * (uniform.globalAmbient + ambientColorSum) +
                          uniform.material.diffuseColor * diffuseColorSum +
                          uniform.material.emissionColor;

    varying.specularColor *= uniform.material.specularColor;
}

float4 getTextureColor(const Varying& fragVars, const Uniform& uniform)
{
    if (!uniform.texture.data || uniform.texture.height <= 0 || uniform.texture.width <= 0)
        return { 1.f, 1.f, 1.f, 1.f };

    const rdrTexture& texture = uniform.texture;

    // Get tex coords with UVs
    float u = wrapValue(fragVars.uv.u, 1.f);
    float v = wrapValue(fragVars.uv.v, 1.f);

    float s = texture.width  * u;
    float t = texture.height * v;

    // Get texel color with tex coords
    float4 texColor;
    if (uniform.textureFilter == FilterType::BILINEAR)
    {
        s -= u;
        t -= v;

        int si = int(s), ti = int(t);

        int index = ti * texture.width + si;

        const float4 colors[4] =
        {
            texture.data[index                    ],  // Top-left
            texture.data[index                 + 1],  // Top-right
            texture.data[index + texture.width    ],  // Bottom-left
            texture.data[index + texture.width + 1],  // Bottom-right
        };

        texColor = bilinear(s - si, t - ti, colors);
    }
    else
        texColor = texture.data[int(t) * texture.width + int(s)];

    return texColor;
}

bool fragmentShader(Varying& fragVars, const Uniform& uniform, float4& outColor)
{
    if (!uniform.lighting)
    {
        outColor = getTextureColor(fragVars, uniform) * fragVars.color;
        return true;
    }

    if (uniform.phongModel)
        getLightColor(uniform, fragVars);

    outColor = getTextureColor(fragVars, uniform) * fragVars.color * fragVars.shadedColor +
               fragVars.specularColor;

    return true;
}

float interpolateFloat(const float3& value, const float3& weight)
{
    return dot(value, weight);
}

Varying interpolateVarying(const Varying varyings[3], const float3& weight)
{
    Varying result;

    float* vr = (float*)&result;
    float* v0 = (float*)&varyings[0];
    float* v1 = (float*)&varyings[1];
    float* v2 = (float*)&varyings[2];

    for (int i = 0; i < sizeof(Varying) / sizeof(float); i++)
        vr[i] = interpolateFloat(float3(v0[i], v1[i], v2[i]), weight);

    return result;
}

bool getBarycentric(const float4 screenCoords[3], const float2& pixelCoords, float inversedArea, float3& inWeights)
{
    // Check if the pixel is in the triangle foreach segment
    inWeights.x = getWeight(screenCoords[1].xy, screenCoords[2].xy, pixelCoords) * inversedArea;
    if (inWeights.x < 0.f)
        return false;

    inWeights.y = getWeight(screenCoords[2].xy, screenCoords[0].xy, pixelCoords) * inversedArea;
    if (inWeights.y < 0.f)
        return false;

    inWeights.z = 1.f - inWeights.x - inWeights.y;
    if (inWeights.z < 0.f)
        return false;

    return true;
}

bool alphaTest(const Uniform& uniform, float alpha)
{
    return alpha >= uniform.cutout;
}

void perspectiveCorrection(const float3 correctionFloats, float3& weight)
{
    weight *= correctionFloats / interpolateFloat(correctionFloats, weight);
}

void rasterTriangle(const Framebuffer& fb, const float4 screenCoords[3], const Varying varying[3], const Uniform& uniform)
{
    // Get the bounding box
    int xMin = min(screenCoords[0].x, min(screenCoords[1].x, screenCoords[2].x));
    int yMin = min(screenCoords[0].y, min(screenCoords[1].y, screenCoords[2].y));
    int xMax = max(screenCoords[0].x, max(screenCoords[1].x, screenCoords[2].x));
    int yMax = max(screenCoords[0].y, max(screenCoords[1].y, screenCoords[2].y));

    float inversedArea = 1.f / getWeight(screenCoords[0].xy, screenCoords[1].xy, screenCoords[2].xy);

    float2 fragment;
    float3 weight;

    for (int i = xMin; i <= xMax; i++)
    {
        fragment.x = i + 0.5f;
        for (int j = yMin; j <= yMax; j++)
        {
            fragment.y = j + 0.5f;

            if (!getBarycentric(screenCoords, fragment, inversedArea, weight))
                continue;

            int fbIndex = j * fb.width + i;

            // Depth test
            float z, *zBuffer = nullptr;
            if (uniform.depthTest)
            {
                z = interpolateFloat({ screenCoords[0].z, screenCoords[1].z, screenCoords[2].z }, weight);

                zBuffer = &fb.depthBuffer[fbIndex];

                if (*zBuffer >= z)
                    continue;
            }

            // Perspective correction
            if (uniform.perspectiveCorrection)
                perspectiveCorrection({ screenCoords[0].w, screenCoords[1].w, screenCoords[2].w }, weight);

            Varying fragVarying = interpolateVarying(varying, weight);

            float4 fragColor;
            if (!fragmentShader(fragVarying, uniform, fragColor))
                continue;

            // Cutout && depth test on
            if (zBuffer && alphaTest(uniform, fragColor.a))
                *zBuffer = z;

            if (uniform.blending)
                fragColor = fragColor * fragColor.a + fb.colorBuffer[fbIndex] * (1.f - fragColor.a);
            else
                fragColor.a = 1.f;

            fb.colorBuffer[fbIndex] = fragColor;
        }
    }
}

float4 vertexShader(const rdrVertex& vertex, const Uniform& uniform, Varying& varying)
{
    // Store triangle vertices positions
    float4 localCoords = uniform.model * float4(vertex.x, vertex.y, vertex.z, 1.f);

    varying.coords = localCoords.xyz / localCoords.w;

    varying.normal = (uniform.model * float4(vertex.nx, vertex.ny, vertex.nz, 0.f)).xyz;

    varying.color = float4(vertex.r, vertex.g, vertex.b, vertex.a) * uniform.globalColor;

    if (!uniform.phongModel && uniform.lighting)
        getLightColor(uniform, varying);

    varying.uv = { vertex.u, vertex.v };

    return uniform.viewProj * localCoords;
}

bool faceCulling(const float3 ndcCoords[3], FaceOrientation orientation, FaceType toCull)
{
    int index1 = 1, index2 = 2;
    if (orientation == FaceOrientation::CCW)
    {
        index1 = 2;
        index2 = 1;
    }

    float normalZ = ((ndcCoords[index2] - ndcCoords[0]) ^ (ndcCoords[index1] - ndcCoords[0])).z;

    switch (toCull)
    {
        case FaceType::BACK: return normalZ > 0.f;

        case FaceType::FRONT: return normalZ < 0.f;

        case FaceType::FRONT_AND_BACK: return normalZ != 0.f;

        case FaceType::NONE: return normalZ == 0.f;

        default: return false;
    }
}

unsigned char computeClipOutcodes(const float4 clipCoords)
{
    unsigned char code = 0;

    for (int i = 0; i < 8; i++)
    {
        // Check for each coordinate if it is outside the plane, if it is change the outcode
        if (sign(i - 3) * clipCoords.e[i % 4] <= -clipCoords.w)
            code |= 1 << i;
    }

    return code;
}

int clipTriangle(clipPoint outputCoords[9], unsigned char outputCodes)
{
    // Fast exit if all points are in the screen
    if (!outputCodes)
        return 3;

    int finalPointCount = 3;

    //Clip against each plane
    for (int i = 0, plane = 1; i < 8; i++, plane <<= 1)
    {
        // If there is no point outside this plane, continue
        if (!(outputCodes & plane))
            continue;

        int currentPointCount = 0;

        clipPoint currentVertices[9];
        const clipPoint* currentVertex  = &outputCoords[0];
        const clipPoint* previousVertex = &outputCoords[finalPointCount - 1];

        // Get axis index and axis sign (-1, 0, 1)
        int axis = i < 4 ? i : i - 4;
        int axisSign = sign(i - 3);

        unsigned char   prevCode  = computeClipOutcodes(previousVertex->coords) & plane;
        float           prevValue = previousVertex->coords.w + axisSign * previousVertex->coords.e[axis];

        while (currentVertex != &outputCoords[finalPointCount])
        {
            unsigned char   currCode  = computeClipOutcodes(currentVertex->coords) & plane;
            float           currValue = currentVertex->coords.w + axisSign * currentVertex->coords.e[axis];

            // Check if only one point is outside the plane
            if (currCode ^ prevCode)
            {
                // Get intersection factor with the current axes
                float lerpFactor = prevValue / (prevValue - currValue);

                // Insert intersection vertex at the end of the array
                currentVertices[currentPointCount++] =
                {
                    // Lerp the values
                    lerp(previousVertex->coords,  currentVertex->coords,  lerpFactor),
                    lerp(previousVertex->weights, currentVertex->weights, lerpFactor)
                };
            }

            //Insert current vertex at the end of the array, if it is inside the plane 
            if (!currCode)
                currentVertices[currentPointCount++] = *currentVertex;

            prevCode  = currCode;
            prevValue = currValue;

            //Move forward (set previous vertex and get next vertex)
            previousVertex = currentVertex++;
        }

        memcpy(outputCoords, currentVertices, sizeof(clipPoint) * currentPointCount);
        finalPointCount = currentPointCount;
    }

    return finalPointCount;
}

void drawTriangle(rdrImpl* renderer, const rdrVertex vertices[3])
{
    Varying varying[3];
    float4 clipCoords[3];
    clipPoint outputPoints[9];
    unsigned char outputCodes[3];

    for (int i = 0; i < 3; i++)
    {
        // Local space (v3) -> Clip space (v4) (apply vertex shader)
        clipCoords[i] = vertexShader(vertices[i], renderer->uniform, varying[i]);

        // Link clip coords and his weight
        float3 weight = { 0.f, 0.f, 0.f };
        weight.e[i] = 1.f;
        outputPoints[i] = { clipCoords[i], weight };

        // Compute clip codes
        outputCodes[i] = computeClipOutcodes(clipCoords[i]);
    }

    // Exit if all vertices are outside the screen
    if (outputCodes[0] & outputCodes[1] & outputCodes[2])
        return;

    int pointCount = clipTriangle(outputPoints, outputCodes[0] | outputCodes[1] | outputCodes[2]);

    if (pointCount < 3) // Exit if there is not enough vertice in the screen
        return;

    // Clip space (v4) to NDC (v3)
    float   invertedW[9];
    float3  ndcCoords[9];

    for (int i = 0; i < pointCount; i++)
    {
        // Compute w
        invertedW[i] = 1.f / outputPoints[i].coords.w;

        // Get ndc coords from new clip coords
        ndcCoords[i] = outputPoints[i].coords.xyz * invertedW[i];
    }

    // Back face culling
    if (faceCulling(ndcCoords, renderer->uniform.faceOrientation, renderer->uniform.faceToCull))
        return;

    float4  screenCoords[9];
    Varying clippedVaryings[9];

    for (int i = 0; i < pointCount; i++)
    {
        // NDC (v3) to screen coords (v2 + depth + clipCoord w)
        screenCoords[i] = { ndcToScreenCoords(ndcCoords[i], renderer->viewport), invertedW[i] };

        // Get new varyings after clipping
        clippedVaryings[i] = interpolateVarying(varying, outputPoints[i].weights);
    }

    // Rasterize triangle by getting the correct screenCoords and varyings
    for (int index0 = 0, index1 = 1, index2 = 2; index2 < pointCount; index1++, index2++)
    {
        const float4 pointCoords[3] = { screenCoords[index0], screenCoords[index1], screenCoords[index2] };

        if (renderer->fillTriangle)
        {
            const Varying varyings[3] = { clippedVaryings[index0], clippedVaryings[index1], clippedVaryings[index2] };
            rasterTriangle(renderer->fb, pointCoords, varyings, renderer->uniform);
        }

        if (renderer->wireframeMode)
        {
            for (int i = 0; i < 3; i++)
                drawLine(renderer->fb, pointCoords[i].xyz, pointCoords[(i + 1) % 3].xyz, renderer->lineColor);
        }
    }
}

void rdrDrawTriangles(rdrImpl* renderer, const rdrVertex* vertices, int count)
{
    renderer->uniform.viewProj = renderer->uniform.projection * renderer->uniform.view;

    // Transform vertex list to triangles into colorBuffer
    for (int i = 0; i < count; i += 3)
        drawTriangle(renderer, &vertices[i]);
}

void rdrSetImGuiContext(rdrImpl* renderer, struct ImGuiContext* context)
{
    ImGui::SetCurrentContext(context);
}

void rdrShowImGuiControls(rdrImpl* renderer)
{
    ImGui::Checkbox("Wireframe", &renderer->wireframeMode);
    ImGui::Checkbox("Depthtest", &renderer->uniform.depthTest);

    // Lighting tree
    if (ImGui::TreeNode("Lighting"))
    {
        ImGui::Checkbox("Lighting", &renderer->uniform.lighting);

        if (renderer->uniform.lighting)
            ImGui::Checkbox("Phong model", &renderer->uniform.phongModel);

        ImGui::TreePop();
    }

    // Texture filtering
    {
        const char* filterTypeStr[] = { "NEAREST", "BILINEAR" };
        int filterTypeIndex = (int)renderer->uniform.textureFilter;
        if (ImGui::Combo("Texture filter", &filterTypeIndex, filterTypeStr, IM_ARRAYSIZE(filterTypeStr)))
            renderer->uniform.textureFilter = FilterType(filterTypeIndex);
    }

    // Face culling tree
    if (ImGui::TreeNode("Face culling"))
    {
        // Face orientation of front-facing polygons
        {
            const char* faceOrientationStr[] = { "Clockwise", "Counter-Clockwise" };
            int faceOrientationIndex = (int)renderer->uniform.faceOrientation;
            if (ImGui::Combo("Face orientation", &faceOrientationIndex, faceOrientationStr, IM_ARRAYSIZE(faceOrientationStr)))
                renderer->uniform.faceOrientation = FaceOrientation(faceOrientationIndex);
        }

        // Face to cull
        {
            const char* faceTypeStr[] = { "None", "Back", "Front", "Front and back" };
            int faceTypeIndex = (int)renderer->uniform.faceToCull;
            if (ImGui::Combo("Face to cull", &faceTypeIndex, faceTypeStr, IM_ARRAYSIZE(faceTypeStr)))
                renderer->uniform.faceToCull = FaceType(faceTypeIndex);
        }
        ImGui::TreePop();
    }

    ImGui::Checkbox("Perspective correction", &renderer->uniform.perspectiveCorrection);
    ImGui::Checkbox("Rasterize triangle", &renderer->fillTriangle);
    ImGui::ColorEdit4("Line color", renderer->lineColor.e);

    ImGui::ColorEdit4("Global ambient", renderer->uniform.globalAmbient.e);
    ImGui::ColorEdit4("Global color", renderer->uniform.globalColor.e);

    // Blending tree
    if (ImGui::TreeNode("Blending"))
    {
        ImGui::Checkbox("Blending", &renderer->uniform.blending);

        if (renderer->uniform.blending)
            ImGui::SliderFloat("Cutout", &renderer->uniform.cutout, 0.f, 1.f);

        ImGui::TreePop();
    }

    // Post-process tree
    if (ImGui::TreeNode("Post-Process"))
    {
        ImGui::Checkbox("Box blur", &renderer->boxBlur);
        ImGui::Checkbox("Gaussian blur", &renderer->gaussianBlur);

        if (ImGui::SliderFloat("Gamma", &renderer->gamma, 0.01f, 10.f));
            renderer->iGamma = 1.f / renderer->gamma;

        ImGui::TreePop();
    }
}