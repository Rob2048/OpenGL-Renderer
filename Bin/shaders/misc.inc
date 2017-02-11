#ifndef _MISC_INC_
#define _MISC_INC_

#define TB_MULTIPLY(T, B, N, V) (

vec3 tbn(vec3 Tangent, vec3 Bitangent, vec3 Normal, vec3 V)
{
	return normalize(mat3(normalize(Tangent), normalize(Bitangent), normalize(Normal)) * V);
}

float saturate(float v) 
{
 	return clamp(v, 0.0f, 1.0f);
}

// TODO: Investigate naming these functions more accurately.
// NOTE: Not entirely accurate. Better solution at http://chilliant.blogspot.co.za/2012/08/srgb-approximations-for-hlsl.html
vec3 toLinear(vec3 RGB)
{
	return pow(RGB, vec3(2.2f));
}

float toLinear(float V)
{
	return pow(V, 2.2f);
}

vec3 toSRGB(vec3 RGB)
{
	return pow(RGB, vec3(1.0f / 2.2f));
}

float toSRGB(float V)
{
	return pow(V, 1.0f / 2.2f);
}

float GetLuma(vec3 V)
{
	return dot(V, vec3(0.2126f, 0.7152f, 0.0722f));
}

float RadicalInverse_VdC(uint Bits)
 {
	Bits = (Bits << 16u) | (Bits >> 16u);
	Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
	Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
	Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
	Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
	return float(Bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 Hammersley(uint I, uint N)
{
	return vec2(float(I) / float(N), RadicalInverse_VdC(I));
}

struct vsDetailMaps
{
	vec3 baseColor;
	vec3 normal;
	float roughness;
};

vsDetailMaps SampleDetailMaps(sampler2D BaseColor, sampler2D NormRough, vec2 Coords, float Scale)
{
	vsDetailMaps maps;

	vec2 detailUV = Coords * Scale;
	vec4 detailNormRough = texture(NormRough, detailUV);
	
	maps.normal = detailNormRough.rgb * 2.0 - 1.0;
	maps.baseColor = texture(BaseColor, detailUV).rgb;
	maps.roughness = (1.0 - toSRGB(detailNormRough.a)) * 2.0 - 1.0;

	return maps;
}

#endif