#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inColor;

layout(binding = 0) uniform sampler2D texMain;
layout(binding = 1) uniform sampler2D texBlur0;
layout(binding = 2) uniform sampler2D texBlur1;
layout(binding = 3) uniform sampler2D texBlur2;
layout(binding = 4) uniform sampler2D texBlur3;
layout(binding = 5) uniform sampler2D texBlur4;
layout(binding = 6) uniform sampler2D texBlur5;
layout(binding = 7) uniform sampler2D texLensDirt;

out vec4 outColor;

float saturate(float V)
{
	return clamp(V, 0.0, 1.0);
}

void main()
{
	vec4 color = texture(texMain, inUV);
	vec3 lensDirt = texture(texLensDirt, inUV).rgb;
	
	vec3 b0 = texture(texBlur0, inUV).rgb;
	vec3 b1 = texture(texBlur1, inUV).rgb;
	vec3 b2 = texture(texBlur2, inUV).rgb;
	vec3 b3 = texture(texBlur3, inUV).rgb;
	vec3 b4 = texture(texBlur4, inUV).rgb;
	vec3 b5 = texture(texBlur5, inUV).rgb;
	
	vec3 bloom = b0 * 0.5f
			 + b1 * 0.8f * 0.75f
			 + b2 * 0.6f
			 + b3 * 0.45f 
			 + b4 * 0.35f
			 + b5 * 0.23f;
	
	bloom /= 2.2;
	
	vec3 lensBloom = b0 * 1.0f + b1 * 0.8f + b2 * 0.6f + b3 * 0.45f + b4 * 0.35f + b5 * 0.23f;
	lensBloom /= 3.2;
	
	float bi = 0.2;
	float ldi = 0.5;
	
	color.rgb = mix(color.rgb, bloom.rgb, vec3(bi));
	color.r = mix(color.r, lensBloom.r, (saturate(lensDirt.r * ldi)));
	color.g = mix(color.g, lensBloom.g, (saturate(lensDirt.g * ldi)));
	color.b = mix(color.b, lensBloom.b, (saturate(lensDirt.b * ldi)));
	
	outColor = vec4(color.rgb, 1.0);
}