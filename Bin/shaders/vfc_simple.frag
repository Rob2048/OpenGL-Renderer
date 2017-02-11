#version 450
#extension GL_ARB_explicit_uniform_location : enable

#include "constants.inc"
#include "misc.inc"
#include "env_map.inc"
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

layout(binding = 4) uniform sampler2D texDetailBC;
layout(binding = 5) uniform sampler2D texDetailNM;
layout(binding = 6) uniform sampler2D texCurve;

layout(location = 1) uniform mat4 matView;

layout(location = 4) uniform vec2 screenSize;
layout(location = 5) uniform vec3 viewPos;

layout(location = 6) uniform vec3 constBaseColor;
layout(location = 7) uniform vec3 constPbrParams;


layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outNormals;

void main()
{	
	vsVirtualTextureInfo vtInfo = GetPhysicalCoords(inUV, texIndirection);
	
	vsBaseMaps maps = SampleVirtualTexture(vtInfo, texChannel0, texChannel1);	
	vsDetailMaps detailMaps = SampleDetailMaps(texDetailBC, texDetailNM, inUV, 4.0f);

	//vec3 norm = tbn(inTangent, inBitangent, inNormal, detailMaps.normal);
	vec3 norm = normalize(inNormal);

	float curve = saturate(texture(texCurve, inUV).r * 1);

	//curve = saturate(curve * -detailMaps.roughness * 100.0);

	//float scratches = saturate(-detailMaps.roughness * 100.0);	
	float scratches = saturate(curve * -detailMaps.roughness * 100.0);
	curve = saturate((curve - 0.6) * 100.0 + detailMaps.roughness);
	curve = saturate(curve + scratches);

	//if (curve > 0.0)
		//curve = 1.0;
	//curve *= detailMaps.baseColor.r;
	//curve = saturate(pow(curve, 10));

	//curve = 0.0;

	/*
	curve -= (150.0 / 255.0);
	curve = saturate(curve);
	curve *= (255.0 / (160 - 150));
	curve = saturate(curve);

	curve = saturate(pow(curve, 10));
	*/

	// Base
	vsMetalRough metalRoughA;
	metalRoughA.baseColor = toLinear(constBaseColor);
	metalRoughA.roughness = clamp(constPbrParams.y + detailMaps.roughness, 0.02, 0.9);
	metalRoughA.metallic = constPbrParams.x;
	metalRoughA.f0 = mix(vec3(0.04f), metalRoughA.baseColor, metalRoughA.metallic);

	// Iron
	vsMetalRough metalRoughB;
	metalRoughB.baseColor = toLinear(vec3(0.776, 0.776, 0.784));
	metalRoughB.roughness = clamp(0.2 + detailMaps.roughness, 0.02, 0.9);
	metalRoughB.metallic = 1.0;
	metalRoughB.f0 = mix(vec3(0.04f), metalRoughB.baseColor, metalRoughB.metallic);

	vsMetalRough metalRoughFinal;
	metalRoughFinal.baseColor = mix(metalRoughA.baseColor, metalRoughB.baseColor, curve) + (detailMaps.baseColor * 0.2);
	metalRoughFinal.roughness = mix(metalRoughA.roughness, metalRoughB.roughness, curve);
	metalRoughFinal.metallic = mix(metalRoughA.metallic, metalRoughB.metallic, curve);
	metalRoughFinal.f0 = mix(vec3(0.04f), metalRoughFinal.baseColor, metalRoughFinal.metallic);
	//metalRoughFinal.f0 = mix(metalRoughA.f0, metalRoughB.f0, curve);

	vec3 N = norm;
	vec3 V = normalize(viewPos - inWS.xyz);
	
	vec3 indirectLighting = CalculateIndirectPBR(N, V, texIrrEnv, texEnv, metalRoughFinal);
	vec3 directLighting = CalculateDirectClusterPBR(screenSize, N, V, metalRoughFinal, inWS);

	//-------------------------------------------------------------------------------------------------
	// Framebuffer Output.
	//-------------------------------------------------------------------------------------------------
	vec3 outFinal = indirectLighting + directLighting;
	//vec3 outFinal = directLighting;
	//vec3 outFinal = vec3(curve);
	
	// NOTE: Possible for there to be negative values here, this is bad for post processing.
	outColor = vec4(max(outFinal, 0), 1.0f);

	// Viewspace Normals GBuffer Target.
	vec3 vsNorms = (matView * vec4(norm, 0.0f)).xyz;	
	outNormals = vec2(vsNorms.x, vsNorms.z);
}