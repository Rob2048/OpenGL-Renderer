#version 450
#extension GL_ARB_explicit_uniform_location : enable

#include "constants.inc"
#include "misc.inc"
#include "env_map.inc"

#define ENABLE_FEEDBACK_BUFFER
#include "virtual_texture.inc"

#define ENABLE_CLUSTERED_LIGHTING
#define ENABLE_SPHERICAL_HARMONICS
#include "lighting.inc"

layout(early_fragment_tests) in;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inWS;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(binding = 0) uniform sampler2D texChannel0;
layout(binding = 1) uniform sampler2D texChannel1;
layout(binding = 2) uniform sampler2D texIndirection;
layout(binding = 3) uniform sampler2D texEnv;
layout(binding = 7) uniform sampler2D texIrrEnv;

layout(location = 1) uniform mat4 matView;

layout(location = 4) uniform vec2 screenSize;
layout(location = 5) uniform vec3 viewPos;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outNormals;

void main()
{	
	vsVirtualTextureInfo vtInfo = GetPhysicalCoords(inUV, texIndirection);
	
	vsBaseMaps maps = SampleVirtualTexture(vtInfo, texChannel0, texChannel1);

	vec3 norm = tbn(inTangent, inBitangent, inNormal, maps.normal);
	
	vsMetalRough metalRough;
	metalRough.baseColor = maps.baseColor;
	metalRough.roughness = maps.roughness;
	metalRough.metallic = maps.metallic;
	metalRough.f0 = mix(vec3(0.04f), metalRough.baseColor, metalRough.metallic);

	vec3 N = norm;
	vec3 V = normalize(viewPos - inWS.xyz);

	vec3 indirectLighting = CalculateIndirectPBR(N, V, texIrrEnv, texEnv, metalRough);
	vec3 directLighting = CalculateDirectClusterPBR(screenSize, N, V, metalRough, inWS);
	
	WriteFeedback(vtInfo, screenSize);

	vec3 outFinal = indirectLighting + directLighting;
	//vec3 outFinal = directLighting;
	// NOTE: Possible for there to be negative values here, this is bad for post processing.
	outColor = vec4(max(outFinal, 0), 1.0f);

	// Viewspace Normals GBuffer Target.
	vec3 vsNorms = (matView * vec4(norm, 0.0f)).xyz;	
	outNormals = vec2(vsNorms.x, vsNorms.z);
}