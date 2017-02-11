#version 450
#extension GL_ARB_explicit_uniform_location : enable

#include "constants.inc"
#include "env_map.inc"

layout(early_fragment_tests) in;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inWS;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(binding = 3) uniform sampler2D texEnv;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outNormals;

void main()
{
	vec3 norm = normalize(inNormal);
	outColor = vec4(sampleEnvMap(texEnv, -norm, 0.0f).rgb, 1.0);
	//outColor = vec4(0, 0, 0, 1);
	outNormals = vec2(0, 0);
}