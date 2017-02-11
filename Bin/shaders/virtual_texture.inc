#ifndef _VIRTUAL_TEXTURE_INC_
#define _VIRTUAL_TEXTURE_INC_

struct vsBaseMaps
{
    vec3 baseColor;
    float roughness;
    float metallic;
    vec3 normal;
};

struct vsVirtualTextureInfo
{
    vec2 coords;
    vec2 physicalCoords;
    float mip;
    float mapSize;
};

float MipLevel(vec2 uv, vec2 tex_size)
{
    vec2 dx_scaled, dy_scaled;
    vec2 coord_scaled = uv * tex_size;

    dx_scaled = dFdx(coord_scaled);
    dy_scaled = dFdy(coord_scaled);

    vec2 dtex = dx_scaled * dx_scaled + dy_scaled * dy_scaled;
    float min_delta = max(dtex.x,dtex.y);
    float miplevel = max(0.5 * log2(min_delta), 0.0);

    return miplevel;
}

vsVirtualTextureInfo GetPhysicalCoords(vec2 UV, sampler2D IndirectionTex)
{
    float mip = MipLevel(UV, vec2(VT_SIZE, VT_SIZE));
    mip = clamp(mip, 0.0, VT_MIP_COUNT - 1.0);
    mip = floor(mip);

    vec3 cachePos = textureLod(IndirectionTex, UV, mip).rgb;
    float sampledMip = floor(cachePos.z * 255.0);
    float diffMip = sampledMip - mip;
    float diffScale = pow(2, diffMip);
    float mapSize = pow(2.0, 18 - mip - 1);
    vec2 pageIndex = UV;
    pageIndex = floor((pageIndex * mapSize / diffScale) / 128);

    vec2 relPos = UV;
    relPos *= mapSize / diffScale;
    relPos = relPos - pageIndex * 128;
    relPos = relPos / 8192.0;

    float texelWidth = 1.0 / 8192.0;
    relPos *= (120.0 / 128.0);
    relPos += texelWidth * 4;

    float vtX = cachePos.x * 255.0 * (128.0 / 8192.0);
    float vtY = cachePos.y * 255.0 * (128.0 / 8192.0);

    vec2 cacheUV = vec2(vtX, vtY) + relPos;

    vsVirtualTextureInfo vtInfo;
    vtInfo.coords = UV;
    vtInfo.physicalCoords = cacheUV;
    vtInfo.mip = mip;
    vtInfo.mapSize = mapSize;

    return vtInfo;
}

vsBaseMaps SampleVirtualTexture(vsVirtualTextureInfo Info, sampler2D Channel0, sampler2D Channel1)
{
    vec4 channel0 = texture(Channel0, Info.physicalCoords);
    vec4 channel1 = texture(Channel1, Info.physicalCoords);

    vec3 baseColor = channel0.rgb;
    // Direct for fuel tank.
    // Double toSRGB for baron?
    float roughness = toSRGB(toSRGB(channel1.r));
    float metallic = toSRGB(channel1.g);

    vec3 normals = vec3(channel0.a, 1.0 - channel1.a, 0.0);
    normals.xy = normals.xy * 2 - 1;
    normals.z = sqrt(1 - saturate(dot(normals.xy, normals.xy)));
    vec3 tsNorm = normalize(normals);

    vsBaseMaps maps;
    maps.baseColor = baseColor;
    maps.roughness = roughness;
    maps.metallic = metallic;
    maps.normal = tsNorm;

    return maps;
}

#ifdef ENABLE_FEEDBACK_BUFFER
    layout(r32ui, binding = 2) uniform uimage2D feedbackBuffer;

    void WriteFeedback(vsVirtualTextureInfo Info, vec2 ScreenSize)
    {
        vec2 fbbSize = vec2(160.0, 120.0);
        vec2 fbbUV = gl_FragCoord.xy / (ScreenSize.xy / fbbSize);
        vec2 frameBufferPixelSize = fbbSize * (1.0 / ScreenSize.xy);
        bool xPass = fract(fbbUV.x) < frameBufferPixelSize.x;
        bool yPass = fract(fbbUV.y) < frameBufferPixelSize.y;

        if (xPass && yPass)
        {
            ivec2 fbbCoords = ivec2(fbbUV);
            uvec2 fbbPageIndex = uvec2(Info.coords * Info.mapSize / 128.0);
            uint fbbPageX = fbbPageIndex.x;
            uint fbbPageY = fbbPageIndex.y;
            uint fbbMip = uint(Info.mip);
            uint fbbPixel = (fbbPageX & 0xFFF) | ((fbbPageY & 0xFFF) << 12) | (fbbMip << 24);
            
            imageStore(feedbackBuffer, fbbCoords, uvec4(fbbPixel, 0, 0, 0));
        }
    }
#endif

#endif