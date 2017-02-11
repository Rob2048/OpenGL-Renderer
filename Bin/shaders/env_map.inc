#ifndef _ENV_MAP_INC_
#define _ENV_MAP_INC_

#include "misc.inc"

vec4 sampleEnvMap(sampler2D EnvMap, vec3 Nv, float Lod)
{
	vec2 tc = vec2(atan(Nv.x, Nv.z), acos(Nv.y / 1.0f));

	tc.x /= 2.0 * PI;
    tc.y /= PI;

    tc.x += 0.0f;
    
    vec4 envSample = textureLod(EnvMap, tc, Lod);
    
    // NOTE: We assume the env map is sampled in srgb, so we need to convert to linear.
    // This is becuase the env map is stored in a non srgb texture, so the GPU doesn't
    // know to do the conversion for us.
    // It is extremely important for an HDR texture to be operated on in linear space.

    // NOTE: Not sure about the above note, lol.

    return vec4(toLinear(envSample.rgb) * 10.0, envSample.a);
}

vec4 sampleEnvMapNoExp(sampler2D EnvMap, vec3 Nv, float Lod)
{
    vec2 tc = vec2(atan(Nv.x, Nv.z), acos(Nv.y / 1.0f));

    tc.x /= 2.0 * PI;
    tc.y /= PI;

    tc.x += 0.0f;
    
    vec4 envSample = textureLod(EnvMap, tc, Lod);
    
    // NOTE: We assume the env map is sampled in srgb, so we need to convert to linear.
    // This is becuase the env map is stored in a non srgb texture, so the GPU doesn't
    // know to do the conversion for us.
    // It is extremely important for an HDR texture to be operated on in linear space.

    // NOTE: Not sure about the above note, lol.

    return vec4(toLinear(envSample.rgb), envSample.a);
}

vec2 GetEnvMapCoord(vec3 Nv)
{
    vec2 tc = vec2(atan(Nv.x, Nv.z), acos(Nv.y / 1.0f));

    tc.x /= 2.0 * PI;
    tc.y /= PI;

    return tc;
}

vec3 GetEnvMapNormal(vec2 Coord)
{
    vec3 result;

    result.x = sin(Coord.x * PI * 2.0) * sin(Coord.y * PI);
    result.y = cos(Coord.y * PI);
    result.z = cos(Coord.x * PI * 2.0) * sin(Coord.y * PI);

    return result;
}

#endif