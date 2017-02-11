#version 450
#extension GL_ARB_explicit_uniform_location : enable

const float PI = 3.14159265359;
const float INV_LOG2 = 1.4426950408889634073599246810019;

const float CLUSTER_NEAR_PLANE = 0.5;
const float CLUSTER_FAR_PLANE = 10000.0;

layout(early_fragment_tests) in;

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inWS;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(binding = 0) uniform sampler2D texChannel0;
layout(binding = 1) uniform sampler2D texChannel1;
layout(binding = 2) uniform sampler2D texIndirection;
layout(binding = 3) uniform sampler2D texEnv;

layout(location = 1) uniform mat4 matView;

layout(location = 4) uniform vec2 screenSize;
layout(location = 5) uniform vec3 viewPos;

layout(r32ui, binding = 2) uniform uimage2D feedbackBuffer;

struct cellData_t
{
	uint itemOffset;
	uint lightCount;
};

struct lightData_t
{
	vec3 position;
	float radius;
	vec4 color;
};

layout(std430, binding = 0) buffer offsetList
{
	cellData_t cellData[];
};

layout(std430, binding = 1) buffer itemList
{
	uint itemData[];
};

layout(std430, binding = 2) buffer lightList
{
	lightData_t lightData[];
};

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outNormals;

float MipLevel(vec2 uv, vec2 tex_size)
{
	vec2 dx_scaled, dy_scaled;
	vec2 coord_scaled = uv * tex_size;

	dx_scaled = dFdx(coord_scaled);
	dy_scaled = dFdy(coord_scaled);

	vec2 dtex = dx_scaled * dx_scaled + dy_scaled * dy_scaled;
	float min_delta = max(dtex.x,dtex.y);
	float miplevel = max(0.5 * log2(min_delta), 0.0);

	return miplevel;
}

float Attenuation(float Distance, float Radius)
{
	float A = pow(Distance / Radius, 4.0);
	float B = clamp(1.0 - A, 0.0, 1.0);
	float falloff = pow(B, 2.0) / (pow(Distance, 2.0) + 1.0);

	return falloff;
}

float saturate(float v) 
{
 	return clamp(v, 0.0f, 1.0f);
}

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

// Calculate the reflectance based on angle of incidence and reflection at zero incidence.
vec3 fresnelSchlick(float CosTheta, vec3 F0)
{
	return F0 + (1.0f - F0) * pow(1.0f - CosTheta, 5.0f);
}

float distributionGGX(vec3 N, vec3 H, float Roughness)
{
	float a = Roughness * Roughness;
	float a2 = a * a;
	float ndh = max(dot(N, H), 0.0f);
	float ndh2 = ndh * ndh;

	float nom = a2;
	float denom = (ndh2 * (a2 - 1.0f) + 1.0f);
	denom = PI * denom * denom;

	return nom / denom;
}

float geometrySchlickGGX(float Ndv, float Roughness)
{
	float r = Roughness + 1.0f;
	float k = (r * r) / 8.0f;

	float nom = Ndv;
	float denom = Ndv * (1.0f - k) + k;

	return nom / denom;
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float Roughness)
{
	float ndv = max(dot(N, V), 0.0f);
	float ndl = max(dot(N, L), 0.0f);
	float ggx2 = geometrySchlickGGX(ndv, Roughness);
	float ggx1 = geometrySchlickGGX(ndl, Roughness);

	return ggx1 * ggx2;
}

vec3 BlinnPhongBRDF()
{
	/*
	vec3 lightDir = normalize(lightToVert);
	vec3 reflectDir = reflect(-lightDir, norm);

	float specPower = 64.0f;
	float specGloss = 0.5f;
	float spec = pow(max(dot(viewDir, reflectDir), 0.0f), specPower);

	specColor += light.color.rgb * att * spec * specGloss;
	lightColor += clamp(dot(lightDir, norm), 0, 1) * light.color.rgb * att;
	*/

	return vec3(0);
}

vec3 CookTorranceBRDF(vec3 N, vec3 V, vec3 L, vec3 F0, vec3 LightColor, vec3 BaseColor, float Metallic, float Roughness, float Attenuation)
{
	vec3 H = normalize(V + L);

	vec3 radiance = LightColor * Attenuation;

	float ndf = distributionGGX(N, H, Roughness);
	float g = geometrySmith(N, V, L, Roughness);
	vec3 F = fresnelSchlick(max(dot(H, V), 0.0f), F0);

	vec3 nom = ndf * g * F; // NOTE: kS Not needed because it is here as F!
	float denom = 4 * max(dot(V, N), 0.0f) * max(dot(L, N), 0.0f) + 0.001f;
	vec3 brdf = nom / denom;

	// Calculate the ratio of spec & diffuse. (reflect vs refract)
	vec3 kS = F;
	vec3 kD = vec3(1.0f) - kS;
	kD *= 1.0f - Metallic;

	float ndl = max(dot(N, L), 0.0f);
	
	//Lo += (kD * pbrBaseColor / PI + brdf) * radiance * ndl;
	vec3 brdfOut = (kD * BaseColor / PI + brdf) * radiance * ndl;

	return brdfOut;
}

vec4 sampleEnvMap(vec3 Nv, float Lod)
{
	vec2 tc = vec2(atan(Nv.x, Nv.z), acos(Nv.y / 1.0f));

	tc.x /= 2.0 * PI;
    tc.y /= PI;

    tc.x += 0.0f;
    
    vec4 envSample = textureLod(texEnv, tc, Lod);
    
    // NOTE: We assume the env map is sampled in srgb, so we need to convert to linear.
    // This is becuase the env map is stored in a non srgb texture, so the GPU doesn't
    // know to do the conversion for us.
    // It is extremely important for an HDR texture to be operated on in linear space.
    return vec4(toLinear(envSample.rgb) * 10.0, envSample.a);
}

vec3 SphericalPoint(vec2 Xi, float Roughness)
{
	float a = Roughness * Roughness;

	// Compute distribution direction.
	float Phi = 2 * PI * Xi.x;
	//float CosTheta = (1.0 - Xi.y);
	//float SinTheta = sqrt(1.0 - CosTheta * CosTheta);	
	float CosTheta = sqrt((1 - Xi.y) / (1 + (a*a - 1) * Xi.y));		
	float SinTheta = sqrt(1 - CosTheta * CosTheta);

	// Convert to spherical distribution.
	vec3 H;
	H.x = SinTheta * cos(Phi);
	H.y = SinTheta * sin(Phi);
	H.z = CosTheta;

	return H;
}

float ComputeLod(uint NumSamples, vec3 N, vec3 H, float Roughness)
{
	//return 7;
	float dist = distributionGGX(N, H, Roughness);
	return 0.5 * (log2(2048.0 / 1024.0 / float(NumSamples)) - log2(dist));
	//return 0.5 * (log2(2048.0 / 1024.0 / float(NumSamples)) - log2(dist));
}

float distortion(vec3 Wn)
{
  // Computes the inverse of the solid angle of the (differential) pixel in
  // the cube map pointed at by Wn
  float sinT = sqrt(1.0-Wn.y*Wn.y);
  return sinT;
}

const float M_INV_LOG2 = 1.4426950408889634073599246810019;

float computeLOD(uint NumSamples, vec3 L, vec3 N, vec3 H, float Roughness)
{
	float maxLod = 11;
	float p = distributionGGX(N, H, Roughness);

  return max(0.0, (maxLod-1.5) - 0.5*(log(float(NumSamples)) + log( p * distortion(L) ))
    * M_INV_LOG2);
}

vec3 Radiance(vec3 N, vec3 V, float Roughness, vec3 F0)
{
	float rough = Roughness;
	vec3 upVector = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangentX = normalize(cross(upVector, N));
	vec3 tangentY = cross(N, tangentX);

	// TODO: Maybe saturate?
	float ndv = abs(dot(N, V));

	vec3 fColor = vec3(0.0);
	const uint numSamples = 20;
	for (uint i = 0; i < numSamples; ++i)
	{
		vec2 Xi = Hammersley(i, numSamples);
		vec3 Li = SphericalPoint(Xi, rough);

		// Convert to world space.
		vec3 H = normalize(Li.x * tangentX + Li.y * tangentY + Li.z * N);
		vec3 L = normalize(-reflect(V, H));

		float ndl = abs(dot(N, L));
		float ndh = abs(dot(N, H));
		float vdh = abs(dot(V, H));
		//float lod = ComputeLod(numSamples, N, H, 1.0 - rough);
		float lod = computeLOD(numSamples, L, N, H, rough);

		vec3 F_ = fresnelSchlick(max(dot(H, V), 0.0f), F0);
		float G_ = geometrySmith(N, V, L, rough);
		vec3 LColor = sampleEnvMap(L, lod).rgb;

		fColor += F_ * G_ * LColor * vdh / (ndh * ndv);
	}

	return fColor / float(numSamples);
}

float GetLuma(vec3 V)
{
	return dot(V, vec3(0.2126f, 0.7152f, 0.0722f));
}

void main()
{	
	//-------------------------------------------------------------------------------------------------
	// Virtual Texturing & Map Assembly.
	//-------------------------------------------------------------------------------------------------
	float mip = MipLevel(inUV, vec2(131072, 131072));
	mip = clamp(mip, 0.0, 10.0);
	mip = floor(mip);

	//outColor = vec4(pageIndex.x * 0.01f, pageIndex.y * 0.01f, 0.0, 1.0);

	vec3 cachePos = textureLod(texIndirection, inUV, mip).rgb;
	float sampledMip = floor(cachePos.z * 255.0);
	float diffMip = sampledMip - mip;
	float diffScale = pow(2, diffMip);
	float mapSize = pow(2.0, 18 - mip - 1);
	vec2 pageIndex = inUV;
	pageIndex = floor((pageIndex * mapSize / diffScale) / 128);

	vec2 relPos = inUV;
	relPos *= mapSize / diffScale;
	relPos = relPos - pageIndex * 128;
	relPos = relPos / 8192.0;

	float texelWidth = 1.0 / 8192.0;
	relPos *= (120.0 / 128.0);
	relPos += texelWidth * 4;

	float vtX = cachePos.x * 255.0 * (128.0 / 8192.0);
	float vtY = cachePos.y * 255.0 * (128.0 / 8192.0);

	vec2 cacheUV = vec2(vtX, vtY) + relPos;

	vec4 channel0 = texture(texChannel0, cacheUV);
	vec4 channel1 = texture(texChannel1, cacheUV);

	vec3 baseColor = channel0.rgb;
	// Direct for fuel tank.
	// Double toSRGB for baron?
	//float roughness = channel1.r * 0.5f;
	float roughness = toSRGB(toSRGB(channel1.r));
	float metallic = toSRGB(channel1.g);

	vec3 normals = vec3(channel0.a, 1.0 - channel1.a, 0.0);
	normals.xy = normals.xy * 2 - 1;
	normals.z = sqrt(1 - saturate(dot(normals.xy, normals.xy)));
	vec3 tsNorm = normalize(normals);

	vec3 tbNorm = normalize(inNormal);
	vec3 tbTan = normalize(inTangent);
	vec3 tbBitan = normalize(inBitangent);

	mat3 tbn = mat3(tbTan, tbBitan, tbNorm);	
	vec3 norm = normalize(tbn * tsNorm);

	//-------------------------------------------------------------------------------------------------
	// PBR.
	//-------------------------------------------------------------------------------------------------
	vec3 pbrBaseColor = baseColor;
	float pbrMetallic = metallic;
	float pbrRoughnesss = roughness;

	vec3 N = norm;
	vec3 V = normalize(viewPos - inWS.xyz);
	vec3 L = normalize(-reflect(V, N));
	vec3 H = normalize(V + L);

	// Surface reflection changes based on metallic(rgb differs)/dielectirc(always 4% gray)
	vec3 F0 = vec3(0.04f);
	F0 = mix(F0, pbrBaseColor, pbrMetallic);

	vec3 iblContrib;

	// Image Based Lighting.
	// TODO: Need to take metallic into account.
	// TODO: Is fresnel reflectance really going on here?
	// TODO: Is energy conservation happening?
	{
		float ndv = saturate(dot(N, V));
		float ndl = saturate(dot(N, L));

		vec3 diffIrr = sampleEnvMap(N, 9.0).rgb;
		// TODO: Watch this PI.
		vec3 kD = diffIrr * pbrBaseColor / PI;
		vec3 kS = Radiance(N, V, pbrRoughnesss, F0);

		vec3 dielectric = kD + kS;
		vec3 conductive = kS * pbrBaseColor;

		iblContrib = mix(dielectric, conductive, pbrMetallic);
	}

	//-------------------------------------------------------------------------------------------------
	// Clustered Lighting.
	//-------------------------------------------------------------------------------------------------
	// TODO: Not sure if this is exactly what we want? Seems to be pretty good.
	float eyeZ = (1.0 / gl_FragCoord.w);

	vec3 cellPos = gl_FragCoord.xyz;
	cellPos.x /= (screenSize.x / 16.0);
	cellPos.y /= (screenSize.y / 8.0);
	cellPos.z = (log(eyeZ / CLUSTER_NEAR_PLANE) / log(CLUSTER_FAR_PLANE / CLUSTER_NEAR_PLANE)) * 24.0 + 1.0f;
	
	int cellX = int(cellPos.x);
	int cellY = int(cellPos.y);
	int cellZ = int(cellPos.z);

	// NOTE: Probably don't have to clamp X and Y due to view frustum always being within cluster frustum.
	cellZ = clamp(cellZ, 1, 24);

	cellData_t cell = cellData[(cellZ * (16 * 8)) + (cellY * 16) + (cellX)];

	vec3 lightColor = vec3(0, 0, 0);
	vec3 specColor = vec3(0, 0, 0);


#if 0
	// NOTE: Show light count, green to red.
	lightColor = mix(vec3(0, 1, 0), vec3(1, 0, 0), float(cell.lightCount) / 30.0);
#else
	for (int i = 0; i < cell.lightCount; ++i)
	{
		uint lightOffset = itemData[cell.itemOffset + i];
		lightData_t light = lightData[lightOffset];
		vec3 lightToVert = light.position.xyz - inWS.xyz;
		float att = Attenuation(length(lightToVert), light.radius);
		vec3 L = normalize(lightToVert);

	#if 1

		if (att > 0)
		{
			lightColor += CookTorranceBRDF(N, V, L, F0, light.color.rgb, pbrBaseColor, pbrMetallic, pbrRoughnesss, att);
		}

	#else

		// NOTE: Show fragment rejection.
		float av = 0;
		if (att > 0) av = 1.0;
		lightColor += vec3(0.2, av, 0);

	#endif
	}

	/*
	// Directional Ambient Diffuse
	vec3 dirTop = clamp(dot(vec3(0, 1, 0), norm), 0, 1) * vec3(1.0, 0.6, 0.5) * 0.01;
	vec3 dirBottom = clamp(dot(vec3(0, -1, 0), norm), 0, 1) * vec3(0.5, 0.6, 1.0) * 0.02;
	lightColor += dirTop + dirBottom;
	*/
	//lightColor += vec3(0.01, 0.01, 0.01);
#endif

	//-------------------------------------------------------------------------------------------------
	// Feedback Buffer.
	//-------------------------------------------------------------------------------------------------
	vec2 fbbSize = vec2(160.0, 120.0);
	vec2 fbbUV = gl_FragCoord.xy / (screenSize.xy / fbbSize);
	vec2 frameBufferPixelSize = fbbSize * (1.0 / screenSize.xy);
	bool xPass = fract(fbbUV.x) < frameBufferPixelSize.x;
	bool yPass = fract(fbbUV.y) < frameBufferPixelSize.y;

	if (xPass && yPass)
	{
		//ivec2 fbbCoords = ivec2(gl_FragCoord.xy / vec2(screenSize.x / 160.0, screenSize.y / 120.0));
		ivec2 fbbCoords = ivec2(fbbUV);
		uvec2 fbbPageIndex = uvec2(inUV * mapSize / 128.0);
		uint fbbPageX = fbbPageIndex.x;
		uint fbbPageY = fbbPageIndex.y;
		uint fbbMip = uint(mip);
		uint fbbPixel = (fbbPageX & 0xFFF) | ((fbbPageY & 0xFFF) << 12) | (fbbMip << 24);
		
		imageStore(feedbackBuffer, fbbCoords, uvec4(fbbPixel, 0, 0, 0));
	}

	//-------------------------------------------------------------------------------------------------
	// Framebuffer Output.
	//-------------------------------------------------------------------------------------------------
	//vec3 outFinal = vec3(roughness);
	//vec3 outFinal = baseColor.rgb;
	//vec3 outFinal = toLinear(tsNorm * 0.5 + 0.5);

	//vec3 outFinal = lightColor + specColor;
	//vec3 outFinal = iblContrib;
	vec3 outFinal = iblContrib + lightColor + specColor;

	// NOTE: Possible for there to be negative values here, this is bad for post processing.
	outColor = vec4(max(outFinal, 0), 1.0f);

	// Viewspace Normals GBuffer Target.
	vec3 vsNorms = (matView * vec4(norm, 0.0f)).xyz;	
	outNormals = vec2(vsNorms.x, vsNorms.z);
}