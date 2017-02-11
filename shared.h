#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <stdint.h>
#include <iostream>
#include <assert.h>

#define ARRAY_COUNT(X) (sizeof(X) / sizeof(X[0]))

typedef uint8_t		u8;
typedef uint16_t	u16;
typedef uint32_t	u32;
typedef uint64_t	u64;

typedef int8_t		i8;
typedef int16_t		i16;
typedef int32_t		i32;
typedef int64_t		i64;

typedef float		f32;
typedef double		f64;

#define WriteBarrier	_WriteBarrier(); _mm_sfence();
#define ReadBarrier		_ReadBarrier(); _mm_lfence();

#define XR_META_SIZE				192
#define XR_META_RGB_SIZE			114
#define XR_META_ALPHA_OFFSET		126
#define XR_META_ALPHA_SIZE			138

struct vsManagedDependency
{
	char Name[128];
	i32 lastUpdated;
	vsManagedDependency* next;
};

double GetTime();

i32 GetMin(i32 A, i32 B);
i32 GetMax(i32 A, i32 B);

u8* CreateImageFromFile(const char* FileName, i32* Width, i32* Height, i32 PixelType = 4);
void FreeImage(u8* ImageData);

