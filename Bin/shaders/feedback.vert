#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outUV;

layout(location = 0) uniform mat4 matProj;
layout(location = 1) uniform mat4 matView;
layout(location = 2) uniform mat4 matModel;

void main()
{
	outColor = inColor;
	outUV = vec2(inUV.x, inUV.y);
	//outUV = inUV;
	gl_Position = matProj * matView * matModel * vec4(inPos, 1.0);
}