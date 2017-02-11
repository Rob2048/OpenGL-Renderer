#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec3 inColor;

out vec4 outColor;

void main()
{	
	outColor = vec4(inColor, 1.0);
}