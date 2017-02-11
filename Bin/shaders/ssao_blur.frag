#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inColor;

layout(binding = 0) uniform sampler2D texSSAO;

uniform sampler2D texMain;

out vec3 outColor;

void main()
{
	//outColor = texture(texSSAO, inUV).rgb;
	//return;

	vec2 texelSize = 1.0f / vec2(textureSize(texSSAO, 0));
	float result = 0.0f;

	for (int x = -2; x < 2; ++x)
	{
		for (int y = -2; y < 2; ++y)
		{
			vec2 offset = vec2(float(x), float(y)) * texelSize;
			result += texture(texSSAO, inUV + offset).r;
		}
	}

	outColor = vec3(result / (4.0f * 4.0f));
}