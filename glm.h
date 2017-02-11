#pragma once

#define GLM_FORCE_RADIANS
#include <glm\glm.hpp>
#include <glm\gtc\matrix_transform.hpp>

typedef glm::vec2 vec2;
typedef glm::vec3 vec3;
typedef glm::vec4 vec4;
typedef glm::mat4 mat4;

typedef glm::ivec3 ivec3;

// NOTE: Using openGL default co-ords.
const vec3 vec3Forward(0, 0, -1);
const vec3 vec3Right(1, 0, 0);
const vec3 vec3Up(0, 1, 0);
const vec3 vec3Zero(0, 0, 0);
const vec3 vec3One(1, 1, 1);