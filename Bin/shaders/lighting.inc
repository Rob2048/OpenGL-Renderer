#ifndef _LIGHTING_INC_
#define _LIGHTING_INC_

#include "brdfs.inc"
#include "spherical_harmonics.inc"

struct cellData_t
{
    uint itemOffset;
    uint lightCount;
};

struct lightData_t
{
    vec3 position;
    float radius;
    vec4 color;
};

#ifdef ENABLE_CLUSTERED_LIGHTING

    layout(std430, binding = 0) buffer offsetList
    {
        cellData_t cellData[];
    };

    layout(std430, binding = 1) buffer itemList
    {
        uint itemData[];
    };

    layout(std430, binding = 2) buffer lightList
    {
        lightData_t lightData[];
    };

    vec3 CalculateDirectClusterPBR(vec2 ScreenSize, vec3 N, vec3 V, vsMetalRough PBR, vec4 WS)
    {
        // TODO: Not sure if this is exactly what we want? Seems to be pretty good.
        float eyeZ = (1.0 / gl_FragCoord.w);

        vec3 cellPos = gl_FragCoord.xyz;
        cellPos.x /= (ScreenSize.x / 16.0);
        cellPos.y /= (ScreenSize.y / 8.0);
        cellPos.z = (log(eyeZ / CLUSTER_NEAR_PLANE) / log(CLUSTER_FAR_PLANE / CLUSTER_NEAR_PLANE)) * 24.0 + 1.0f;
        
        int cellX = int(cellPos.x);
        int cellY = int(cellPos.y);
        int cellZ = int(cellPos.z);

        // NOTE: Probably don't have to clamp X and Y due to view frustum always being within cluster frustum.
        cellZ = clamp(cellZ, 1, 24);

        cellData_t cell = cellData[(cellZ * (16 * 8)) + (cellY * 16) + (cellX)];

        vec3 lightColor = vec3(0, 0, 0);
        vec3 specColor = vec3(0, 0, 0);

    #if 0
        // NOTE: Show light count, green to red.
        lightColor = mix(vec3(0, 1, 0), vec3(1, 0, 0), float(cell.lightCount) / 30.0);
    #else
        for (int i = 0; i < cell.lightCount; ++i)
        {
            uint lightOffset = itemData[cell.itemOffset + i];
            lightData_t light = lightData[lightOffset];
            vec3 lightToVert = light.position.xyz - WS.xyz;
            float att = Attenuation(length(lightToVert), light.radius);
            vec3 L = normalize(lightToVert);

        #if 1

            if (att > 0)
            {
                lightColor += CookTorranceBRDF(N, V, L, PBR.f0, light.color.rgb, PBR.baseColor, PBR.metallic, PBR.roughness, att);
            }

        #else

            // NOTE: Show fragment rejection.
            float av = 0;
            if (att > 0) av = 1.0;
            lightColor += vec3(0.2, av, 0);

        #endif
        }

    #endif

        return lightColor + specColor;
    }

#endif

const float shconst1 = 0.282095;
const float shconst2 = 0.488603;
const float shconst3 = 1.092548;
const float shconst4 = 0.315392;
const float shconst5 = 0.546274;

const vec3 SH[9] =
{
    vec3(0.79, 0.44, 0.54),
    vec3(0.39, 0.35, 0.60),
    vec3(-0.34, -0.18, -0.27),
    vec3(-0.29, -0.06, 0.01),
    vec3(-0.11, -0.05, -0.12),
    vec3(-0.26, -0.22, -0.47),
    vec3(-0.16, -0.09, -0.15),
    vec3(0.56, 0.21, 0.14),
    vec3(0.21, -0.05, -0.3),
};

vec3 GetSH(vec3 Dir, vec3 SH[9])
{
    vec3 result = SH[0] * shconst1;

    result += SH[1] * shconst2 * -Dir.x;
    result += SH[2] * shconst2 * -Dir.y;
    result += SH[3] * shconst2 * Dir.z;

    result += SH[4] * shconst3 * -(Dir.x * Dir.z);
    result += SH[5] * shconst3 * -(Dir.z * Dir.y);
    result += SH[6] * shconst3 * (Dir.y * Dir.x);

    //result += SH[7] * (3.0f * Dir.z * Dir.z - 1.0);
    result += SH[7] * shconst4 * (-Dir.x * Dir.x - Dir.y * Dir.y + 2.0 * Dir.z * Dir.z);

    result += SH[8] * shconst5 * (Dir.x * Dir.x - Dir.y * Dir.y);   

    return result;
}

vec3 CalculateIndirectPBR(vec3 N, vec3 V, sampler2D IrrMap, sampler2D EnvMap, vsMetalRough PBR)
{
    // TODO: Is fresnel reflectance really going on here?
    // TODO: Is energy conservation happening?

    //float ndv = saturate(dot(Lv.n, Lv.v));
    //float ndl = saturate(dot(Lv.n, Lv.l));

    //vec3 diffIrr = sampleEnvMap(EnvMap, N, 9.0).rgb;
    //vec3 diffIrr = sampleEnvMap(IrrMap, N, 0.0).rgb;
    //vec3 diffIrr = GetSH(N, SH);
    vec3 diffIrr = EvalSH(N);
    //vec3 diffIrr = vec3(0);
    // TODO: Watch this PI.
    //vec3 kD = diffIrr * PBR.baseColor / PI;
    vec3 kD = diffIrr * PBR.baseColor;
    vec3 kS = Radiance(N, V, PBR.roughness, PBR.f0, EnvMap);

    vec3 dielectric = kD + kS;
    vec3 conductive = kS * PBR.baseColor;

    //return vec3(0.0);

    return mix(dielectric, conductive, PBR.metallic);    
}

#endif