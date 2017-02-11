#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec2 inUV;
layout(location = 1) in vec3 inColor;

layout(location = 3) uniform vec2 blurOffset;

layout(binding = 0) uniform sampler2D texMain;

out vec4 outColor;

const vec4 curve4[7] =
{	
	vec4(0.0205,0.0205,0.0205,0),
	vec4(0.0855,0.0855,0.0855,0),
	vec4(0.232,0.232,0.232,0),
	vec4(0.324,0.324,0.324,1),
	vec4(0.232,0.232,0.232,0),
	vec4(0.0855,0.0855,0.0855,0),
	vec4(0.0205,0.0205,0.0205,0)
};

void main()
{
	vec2 netFilterWidth = blurOffset;
	vec2 coords = inUV - netFilterWidth * 3.0;
	
	vec4 color = vec4(0.0);
	
	for (int i = 0; i < 7; ++i)
	{
		vec4 tap = texture(texMain, coords);
		color += tap * curve4[i];
		coords += netFilterWidth;
	}
	
	outColor = vec4(color.rgb, 1.0);	
}