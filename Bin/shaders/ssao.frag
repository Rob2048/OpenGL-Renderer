#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inViewRay;

layout(location = 2) uniform vec4 screenSize;
layout(location = 3) uniform mat4 projMat;
layout(location = 4) uniform vec3 samples[64];

layout(binding = 0) uniform sampler2D texDepth;
layout(binding = 1) uniform sampler2D texNormals;
layout(binding = 2) uniform sampler2D texNoise;

out vec3 outColor;

float saturate(float V)
{
	return clamp(V, 0.0f, 1.0f);
}

void main()
{
	const float f = 1000.0f;
	const float n = 0.01f;
	float projA = f / (f - n);
	float projB = (-f * n) / (f - n);
	float depth = texture2D(texDepth, inUV).r;
	float ld1 = projB / (depth - projA);
	vec3 vsPos = -inViewRay * ld1;

	if (depth == 1.0)
	{
		outColor = vec3(1, 1, 1);
		return;
	}

	vec2 norms = texture(texNormals, inUV).rg;
	vec3 normals = vec3(norms, 0.0f);
	normals.z = sqrt(1 - saturate(dot(normals.xy, normals.xy)));
	normals = normalize(normals);
	vec3 fnorms = vec3(normals.x, normals.z, normals.y);

	vec3 fragPos = vsPos;
	vec3 normal = fnorms;
	vec2 noiseUV = inUV * (screenSize.xy / vec2(4.0f));
	vec3 randomVec = texture(texNoise, noiseUV).xyz;

	vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	vec3 bitangent = cross(normal, tangent);
	mat3 TBN = mat3(tangent, bitangent, normal);

	const uint kernelSize = 64;	
	const float radius = 0.5f;
	const float bias = 0.025f;

	float occlusion = 0.0f;

	float testDepth = 0.0f;

	for (uint i = 0; i < kernelSize; ++i)
	{
		vec3 sampleVS = TBN * samples[i];
		sampleVS = fragPos + sampleVS * radius;

		vec3 viewRay = vec3(sampleVS.xy / sampleVS.z, 1.0f);

		vec4 offset = vec4(sampleVS, 1.0);
		offset = projMat * offset;
		offset.xyz /= offset.w;
		offset.xyz = offset.xyz * 0.5 + 0.5;

		float sampleDepth = texture(texDepth, offset.xy).r;
		sampleDepth = projB / (sampleDepth - projA);
		sampleDepth = (-viewRay * sampleDepth).z;

		// range check & accumulate
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= sampleVS.z + bias ? 1.0 : 0.0) * rangeCheck;
	}

	occlusion = 1.0 - (occlusion / float(kernelSize));
	
	outColor = vec3(occlusion);
	return;

	/*
	outColor = normals * 0.5f + 0.5f;
	return;

	float depth = texture2D(texDepth, inUV).r;
	depth = LinearDepth(depth) * 77;
	outColor = vec3(depth);
	return;	

	
	//outColor = vec4(texture(texNoise, noiseUV).rgb, 1);
	//outColor = vec4(texture(texMain, inUV).rgb * inColor, 1.0) * colorMod;
	*/
}