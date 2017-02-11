#version 450
#extension GL_ARB_explicit_uniform_location : enable

#include "constants.inc"
#include "misc.inc"
#include "env_map.inc"

#include "brdfs.inc"

#define ENABLE_SPHERICAL_HARMONICS
#include "spherical_harmonics.inc"

layout(early_fragment_tests) in;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inWS;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(location = 1) uniform mat4 matView;

layout(binding = 0) uniform sampler2D texEnv;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outNormals;

vec3 SampleEnv(vec3 N)
{
    vec3 upVector = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangentX = normalize(cross(upVector, N));
    vec3 tangentY = cross(N, tangentX);

	vec3 fColor = vec3(0.0);
	const uint numSamples = 4096;
	for (uint i = 0; i < numSamples; ++i)
	{
		vec2 Xi = Hammersley(i, numSamples);
		vec3 Li = SphericalPoint(Xi);

		vec3 H = normalize(Li.x * tangentX + Li.y * tangentY + Li.z * N);

		fColor += sampleEnvMapNoExp(texEnv, H, 8).rgb / PI;
	}

	return fColor / float(numSamples) * 10.0;
}

void main()
{	
	vec3 norm = normalize(inNormal);
	
	vec3 outFinal = EvalSH(norm);
	//outFinal = sampleEnvMapNoExp(texEnv, norm, 8.5).rgb * 10;
	//outFinal = SampleEnv(norm);

	outColor = vec4(max(outFinal, 0), 1.0f);
	// Viewspace Normals GBuffer Target.
	vec3 vsNorms = (matView * vec4(norm, 0.0f)).xyz;
	outNormals = vec2(vsNorms.x, vsNorms.z);
}