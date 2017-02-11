#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent;

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec4 outWS;
layout(location = 3) out vec3 outTangent;
layout(location = 4) out vec3 outBitangent;

layout(location = 0) uniform mat4 matProj;
layout(location = 1) uniform mat4 matView;
layout(location = 2) uniform mat4 matModel;
layout(location = 3) uniform vec4 uvScaleBias;

void main()
{
	outUV = inUV * uvScaleBias.xy + uvScaleBias.zw;
	
	vec3 wNormal = (matModel * vec4(inNormal, 0.0)).xyz;
	vec3 wTangent = (matModel * vec4(inTangent.xyz, 0.0)).xyz;
	vec3 wBitangent = cross(wNormal, wTangent) * inTangent.w;

	outNormal = wNormal;
	outTangent = wTangent;
	outBitangent = wBitangent;

	outWS = matModel * vec4(inPos, 1.0);

	gl_Position = matProj * matView * outWS;
}