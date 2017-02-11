#version 450
#extension GL_ARB_explicit_uniform_location : enable

#include "constants.inc"

layout(early_fragment_tests) in;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inWS;

layout(location = 4) uniform vec2 screenSize;

layout(binding = 0) uniform sampler2D texMain;

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

out vec4 outColor;

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

float Attenuation(vec3 LightToVert, float Radius)
{
	float distance = length(LightToVert);
	float A = pow(distance / Radius, 4.0);
	float B = clamp(1.0 - A, 0.0, 1.0);
	float falloff = pow(B, 2.0) / (pow(distance, 2.0) + 1.0);

	return falloff;
}

void main()
{	
	//-------------------------------------------------------------------------------------------------
	// Texture Sampling.
	//-------------------------------------------------------------------------------------------------
	vec2 tUV = inUV;
	tUV.y = 1.0 - tUV.y;
	vec3 baseColor = texture(texMain, tUV).rgb;

	//-------------------------------------------------------------------------------------------------
	// Clustered Lighting.
	//-------------------------------------------------------------------------------------------------
	vec3 norm = normalize(inNormal);

	// TODO: Not sure if this is exactly what we want? Seems to be pretty good.
	float eyeZ = (1.0 / gl_FragCoord.w);

	vec3 cellPos = gl_FragCoord.xyz;
	cellPos.x /= (screenSize.x / 16.0);
	cellPos.y /= (screenSize.y / 8.0);
	cellPos.z = (log(eyeZ / CLUSTER_NEAR_PLANE) / log(CLUSTER_FAR_PLANE / CLUSTER_NEAR_PLANE)) * 24.0 + 1.0f;
	
	int cellX = int(cellPos.x);
	int cellY = int(cellPos.y);
	int cellZ = int(cellPos.z);

	// NOTE: Probably don't have to clamp X and Y due to view frustum always being within cluster frustum.
	cellZ = clamp(cellZ, 1, 24);

	cellData_t cell = cellData[(cellZ * (16 * 8)) + (cellY * 16) + (cellX)];

	vec3 lightColor = vec3(0, 0, 0);

#if 0
	// NOTE: Show light count, green to red.
	lightColor = mix(vec3(0, 1, 0), vec3(1, 0, 0), float(cell.lightCount) / 30.0);
#else
	for (int i = 0; i < cell.lightCount; ++i)
	{
		uint lightOffset = itemData[cell.itemOffset + i];
		lightData_t light = lightData[lightOffset];
		vec3 lightToVert = light.position.xyz - inWS.xyz;
		float att = Attenuation(lightToVert, light.radius);

	#if 1

		if (att > 0)
		{
			vec3 lightDir = normalize(lightToVert);
			lightColor += clamp(dot(lightDir, norm), 0, 1) * light.color.rgb * att * 1.0;
		}

	#else

		// NOTE: Show fragment rejection.
		float av = 0;
		if (att > 0) av = 1.0;
		lightColor += vec3(0.2, av, 0);

	#endif
	}

	vec3 dirTop = clamp(dot(vec3(0, 1, 0), norm), 0, 1) * vec3(1.0, 0.6, 0.5) * 0.01;
	vec3 dirBottom = clamp(dot(vec3(0, -1, 0), norm), 0, 1) * vec3(0.5, 0.6, 1.0) * 0.02;
	lightColor += dirTop + dirBottom;
	//lightColor += vec3(0.01, 0.01, 0.01);
#endif

	//-------------------------------------------------------------------------------------------------
	// Framebuffer Output.
	//-------------------------------------------------------------------------------------------------
	outColor = vec4(baseColor.rgb * lightColor, 1.0);
}