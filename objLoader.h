#pragma once

#include "shared.h"
#include "glm.h"

struct vsVertex
{
	vec3 pos;
	vec2 uv;
	vec3 normal;
	vec4 tangent;
};

struct vsOBJModel
{
	vsVertex*	verts;
	u16*		indices;
	int			vertCount;
	int			indexCount;
};

vsOBJModel CreateOBJ(const char* FileName, vec4 UVScaleBias);
void CalculateTangents(vsOBJModel* Model);