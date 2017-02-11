#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inColor;

layout(location = 3) uniform vec4 exposureParams;

uniform sampler2D texMain;

out vec4 outColor;

// NOTE: This is Filmic Tonemapping (John Hable: http://filmicgames.com/archives/75)

float A = 0.15f;
float B = 0.50f;
float C = 0.10f;
float D = 0.20f;
float E = 0.02f;
float F = 0.30f;
float W = 11.2f;

vec3 FilmicTonemap(vec3 V)
{
	return ((V * (A * V + C * B) + D * E) / (V * (A * V + B) + D * F)) - E / F;
}

void main()
{
	vec3 hdr = texture(texMain, inUV).rgb;
	float exposure = exposureParams.r;
	hdr *= exposure;

	float exposureBias = 2.0f;
	vec3 curr = FilmicTonemap(exposureBias * hdr);
	vec3 whiteScale = vec3(1.0f) / FilmicTonemap(vec3(W));
	vec3 color = curr * whiteScale;
	
	// NOTE: We are rendering to an SRGB framebuffer, therefore we don't need to gamme correct here.
	//color = pow(color, vec3(1.0f / 2.2f));

	outColor = vec4(color, 1.0f);
}