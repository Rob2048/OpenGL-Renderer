#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec2 outUV;
layout(location = 1) out vec3 outViewRay;

layout(location = 0) uniform vec4 scaleBias;
layout(location = 1) uniform mat4 invProjMat;

void main()
{
	outUV = inUV;
	gl_Position = vec4(inPos * scaleBias.xy * 2.0 + scaleBias.zw * 2.0, 0.0, 1.0);

	vec4 positionVS = invProjMat * vec4(gl_Position.xy, 1.0f, 1.0f);
	outViewRay = vec3(positionVS.xy / positionVS.z, 1.0f);
}