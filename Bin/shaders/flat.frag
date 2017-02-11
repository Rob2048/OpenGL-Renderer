#version 450
#extension GL_ARB_explicit_uniform_location : enable

#include "constants.inc"

layout(early_fragment_tests) in;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inWS;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(location = 4) uniform vec4 constColor;

layout(location = 0) out vec4 outColor;

void main()
{	
	outColor = vec4(constColor.xyz, 1);
}