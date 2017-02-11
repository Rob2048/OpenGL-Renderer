#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inColor;

layout(location = 3) uniform vec4 colorMod;

uniform sampler2D texMain;

out vec4 outColor;

void main()
{
	outColor = vec4(texture(texMain, inUV).rgb * inColor, 1.0) * colorMod;
}