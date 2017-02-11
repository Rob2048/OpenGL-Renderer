#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inColor;

layout(binding = 0) uniform sampler2D texMain;
layout(binding = 1) uniform sampler2D texSSAO;

out vec4 outColor;

void main()
{
	vec2 texelSize = 1.0 / vec2(1280.0, 720.0);

	float ssao = texture(texSSAO, inUV + vec2(texelSize.x, 0)).r;
	ssao = max(ssao, 0.0f);
	ssao = pow(ssao, 4.0f);

	vec3 color = texture(texMain, inUV).rgb;

	//outColor = vec4(color, 1.0);
	outColor = vec4(color * ssao, 1.0);
}