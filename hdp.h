#pragma once

#include "shared.h"

void HdpEncodeImageRGBA(const char* OutputName, u8* Data, i32 Width, i32 Height);
u8* HdpDecodeImageRGBA(const char* FileName, i32* Width, i32* Height);
bool HdpEncodeImageRGBA(u8* Data, i32 Width, i32 Height, u8* OutData, i32* OutDataSize);

void HdpBGRAToRGBABlockStream(u8* InputData, u8* OutputData);
bool HdpDecodeImageBGRA(u8* InputData, i32 InputDataSize, u8* OutputData);