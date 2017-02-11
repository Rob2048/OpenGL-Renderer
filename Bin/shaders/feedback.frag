#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;

layout(location = 3) uniform vec2 screenSize;

uniform sampler2D texMain;

out vec4 outColor;

float MipLevel(vec2 uv, vec2 tex_size)
{
	vec2 dx_scaled, dy_scaled;
	vec2 coord_scaled = uv * tex_size;
//* vec2(160.0, 120.0) / screenSize
	//vec2 dx = ddx(uv * SUB_TEXTURE_SIZE) * (160.0 / gGlobalScreen.x) * ffb;
	//vec2 dy = ddy(uv * SUB_TEXTURE_SIZE) * (120.0 / gGlobalScreen.y) * ffb;


	dx_scaled = dFdx(coord_scaled) * (160.0 / screenSize.x);
	dy_scaled = dFdy(coord_scaled) * (120.0 / screenSize.y);

	vec2 dtex = dx_scaled * dx_scaled + dy_scaled * dy_scaled;
	float min_delta = max(dtex.x,dtex.y);
	float miplevel = max(0.5 * log2(min_delta), 0.0);

	return miplevel;
}

void main()
{
	// TODO: Scale mip texture with screen ratios.
	float mip = MipLevel(inUV, vec2(131072, 131072));
	float bias = -0.5;
	mip = clamp(mip + bias, 0.0, 10.0);
	mip = floor(mip);
	
	vec2 pageIndex = inUV;
	float mapSize = pow(2.0, 18 - mip - 1);
	pageIndex = floor(pageIndex * mapSize / 128.0);

	outColor = vec4(pageIndex.x, pageIndex.y, mip, 1.0);
	//outColor = vec4(pageIndex.x, pageIndex.y, 0.0, 1.0);
}