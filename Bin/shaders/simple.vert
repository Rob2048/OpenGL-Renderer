#version 450
#extension GL_ARB_explicit_uniform_location : enable

layout(location = 0) in vec3 gl_Vertex;
layout(location = 1) in vec3 gl_Color;

layout(location = 0) out vec3 outColor;

layout(location = 0) uniform mat4 matProj;
layout(location = 1) uniform mat4 matView;
layout(location = 2) uniform mat4 matModel;

void main()
{
	outColor = gl_Color;
	gl_Position = matProj * matView * matModel * vec4(gl_Vertex, 1.0);
}