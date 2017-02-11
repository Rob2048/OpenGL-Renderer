#include "hdp.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wincodec.h>

void HdpEncodeImageRGBA(const char* OutputName, u8* Data, i32 Width, i32 Height)
{	
	IWICImagingFactory*		factory = NULL;
	IWICBitmapEncoder*		encoder = NULL;
	IWICBitmapFrameEncode*	bitmapFrame = NULL;
	IPropertyBag2*			propertyBag = NULL;
	IWICStream*				stream = NULL;

	HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)&factory);
	assert(SUCCEEDED(result));

	result = factory->CreateStream(&stream);
	assert(SUCCEEDED(result));

	wchar_t fileNameBuffer[256];
	mbstowcs(fileNameBuffer, OutputName, ARRAY_COUNT(fileNameBuffer));
	size_t nameLen = strlen(OutputName);
	fileNameBuffer[nameLen - 3] = 'j';
	fileNameBuffer[nameLen - 2] = 'x';
	fileNameBuffer[nameLen - 1] = 'r';
	
	//result = stream->InitializeFromFilename(L"output.tif", GENERIC_WRITE);
	result = stream->InitializeFromFilename(fileNameBuffer, GENERIC_WRITE);	
	assert(SUCCEEDED(result));

	//result = factory->CreateEncoder(GUID_ContainerFormatTiff, NULL, &encoder);
	result = factory->CreateEncoder(GUID_ContainerFormatWmp, NULL, &encoder);	
	assert(SUCCEEDED(result));

	result = encoder->Initialize(stream, WICBitmapEncoderNoCache);
	assert(SUCCEEDED(result));

	result = encoder->CreateNewFrame(&bitmapFrame, &propertyBag);
	assert(SUCCEEDED(result));

	/*
	PROPBAG2 option = { 0 };
	option.pstrName = L"TiffCompressionMethod";
	VARIANT varValue;
	VariantInit(&varValue);
	varValue.vt = VT_UI1;
	varValue.bVal = WICTiffCompressionZIP;

	result = propertyBag->Write(1, &option, &varValue);
	assert(SUCCEEDED(result));
	*/

	{
		PROPBAG2 option = {};
		option.pstrName = L"ImageQuality";
		VARIANT varValue;
		VariantInit(&varValue);
		varValue.vt = VT_R4;
		varValue.fltVal = 0.7f;
	
		result = propertyBag->Write(1, &option, &varValue);
		assert(SUCCEEDED(result));
	}

	/*
	{
		PROPBAG2 option = {};
		option.pstrName = L"StreamOnly";
		VARIANT varValue;
		VariantInit(&varValue);
		varValue.vt = VT_BOOL;
		varValue.boolVal = VARIANT_TRUE;

		result = propertyBag->Write(1, &option, &varValue);
		assert(SUCCEEDED(result));
	}
	//*/

	{ PROPBAG2 option = {}; option.pstrName = L"UseCodecOptions"; VARIANT varValue; VariantInit(&varValue); varValue.vt = VT_BOOL; 
		varValue.boolVal = VARIANT_TRUE;
		result = propertyBag->Write(1, &option, &varValue);
		assert(SUCCEEDED(result));
	}

	{
		PROPBAG2 option = {};
		option.pstrName = L"HorizontalTileSlices";
		VARIANT varValue;
		VariantInit(&varValue);
		varValue.vt = VT_UI2;
		varValue.uiVal = 32;

		result = propertyBag->Write(1, &option, &varValue);
		assert(SUCCEEDED(result));
	}

	{
		PROPBAG2 option = {};
		option.pstrName = L"VerticalTileSlices";
		VARIANT varValue;
		VariantInit(&varValue);
		varValue.vt = VT_UI2;
		varValue.uiVal = 32;

		result = propertyBag->Write(1, &option, &varValue);
		assert(SUCCEEDED(result));
	}

	{
		PROPBAG2 option = {};
		option.pstrName = L"Subsampling";
		VARIANT varValue;
		VariantInit(&varValue);
		varValue.vt = VT_UI1;
		varValue.cVal = 2;

		result = propertyBag->Write(1, &option, &varValue);
		assert(SUCCEEDED(result));
	}

	{
		PROPBAG2 option = {};
		option.pstrName = L"Quality";
		VARIANT varValue;
		VariantInit(&varValue);
		varValue.vt = VT_UI1;
		varValue.cVal = 32;

		result = propertyBag->Write(1, &option, &varValue);
		assert(SUCCEEDED(result));
	}

	result = bitmapFrame->Initialize(propertyBag);
	assert(SUCCEEDED(result));

	result = bitmapFrame->SetSize(Width, Height);
	assert(SUCCEEDED(result));

	WICPixelFormatGUID formatGUID = GUID_WICPixelFormat24bppBGR;
	result = bitmapFrame->SetPixelFormat(&formatGUID);
	assert(SUCCEEDED(result));

	result = IsEqualGUID(formatGUID, GUID_WICPixelFormat24bppBGR) ? S_OK : E_FAIL;
	assert(SUCCEEDED(result));

	UINT cbStride = (Width * 24 + 7) / 8;
	UINT cbBufferSize = Height * cbStride;

	BYTE *pbBuffer = new BYTE[cbBufferSize];

	i32 pixelCount = Width * Height;

	for (i32 i = 0; i < pixelCount; i++)
	{
		pbBuffer[i * 3 + 0] = Data[i * 4 + 2];
		pbBuffer[i * 3 + 1] = Data[i * 4 + 1];
		pbBuffer[i * 3 + 2] = Data[i * 4 + 0];
	}

	result = bitmapFrame->WritePixels(Height, cbStride, cbBufferSize, pbBuffer);

	delete[] pbBuffer;

	assert(SUCCEEDED(result));

	result = bitmapFrame->Commit();
	assert(SUCCEEDED(result));

	result = encoder->Commit();
	assert(SUCCEEDED(result));

	factory->Release();
	bitmapFrame->Release();
	encoder->Release();
	stream->Release();
}

u8* HdpDecodeImageRGBA(const char* FileName, i32* Width, i32* Height)
{
	IWICImagingFactory*		factory = NULL;
	IWICBitmapDecoder*		decoder = NULL;
	IWICBitmapFrameDecode*	bitmapFrame = NULL;
	IPropertyBag2*			propertyBag = NULL;
	
	HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)&factory);
	assert(SUCCEEDED(result));

	wchar_t fileNameBuffer[256];
	mbstowcs(fileNameBuffer, FileName, ARRAY_COUNT(fileNameBuffer));
	
	const GUID vendorGUID = GUID_ContainerFormatWmp;
	result = factory->CreateDecoderFromFilename(fileNameBuffer, &vendorGUID, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
	assert(SUCCEEDED(result));

	result = decoder->GetFrame(0, &bitmapFrame);
	assert(SUCCEEDED(result));

	UINT w, h;
	bitmapFrame->GetSize(&w, &h);
	WICPixelFormatGUID pixelFormat;
	bitmapFrame->GetPixelFormat(&pixelFormat);

	assert(w == h);
	*Width = w;
	*Height = h;

	WICRect srcRect = {};
	srcRect.X = 0;
	srcRect.Y = 0;
	srcRect.Height = h;
	srcRect.Width = w;

	i32 bufferSize = w * h * 3;
	u8* buffer = new u8[bufferSize];

	result = bitmapFrame->CopyPixels(&srcRect, 3 * w, bufferSize, buffer);
	assert(SUCCEEDED(result));
	
	// Copy debug info onto page.

	// Assemble block stream.
	i32 horzBlockCount = w / 4;
	i32 blockCount = horzBlockCount * horzBlockCount;
	u8* blockBuffer = new u8[blockCount * 16 * 4];

	// Assemble each block (4x4 neighbouring pixels)
	for (i32 b = 0; b < blockCount; ++b)
	{
		i32 blockOffset = b * 16 * 4;
		i32 blockX = b % horzBlockCount;
		i32 blockY = b / horzBlockCount;
		i32 pixelX = blockX * 4;
		i32 pixelY = blockY * 4;

		for (i32 bY = 0; bY < 4; ++bY)		
		{
			for (i32 bX = 0; bX < 4; ++bX)
			{
				i32 pixelIndex = (pixelY + bY) * w + (pixelX + bX);
				i32 pixelOffset = pixelIndex * 3;

				i32 blockPixelOffset = (bY * 4 + bX) * 4;
				blockBuffer[blockOffset + blockPixelOffset + 0] = buffer[pixelOffset + 2];
				blockBuffer[blockOffset + blockPixelOffset + 1] = buffer[pixelOffset + 1];
				blockBuffer[blockOffset + blockPixelOffset + 2] = buffer[pixelOffset + 0];
				blockBuffer[blockOffset + blockPixelOffset + 3] = 0;

				if (pixelX + bX <= 1 || pixelX + bX >= 126 || pixelY + bY <= 1 || pixelY + bY >= 126)
				{
					blockBuffer[blockOffset + blockPixelOffset + 0] = 255;
					blockBuffer[blockOffset + blockPixelOffset + 1] = 0;
					blockBuffer[blockOffset + blockPixelOffset + 2] = 0;
				}
			}
		}
	}

	delete[] buffer;
	factory->Release();
	bitmapFrame->Release();
	decoder->Release();
	
	return blockBuffer;
}

__forceinline void WriteBagPropR4(IPropertyBag2* PropertyBag, LPOLESTR Name, float Value)
{
	PROPBAG2 option = {};
	option.pstrName = Name;

	VARIANT varValue;
	VariantInit(&varValue);
	varValue.vt = VT_R4;
	varValue.fltVal = Value;

	HRESULT result = PropertyBag->Write(1, &option, &varValue);
	assert(SUCCEEDED(result));
}

__forceinline void WriteBagPropUI2(IPropertyBag2* PropertyBag, LPOLESTR Name, u16 Value)
{
	PROPBAG2 option = {};
	option.pstrName = Name;

	VARIANT varValue;
	VariantInit(&varValue);
	varValue.vt = VT_UI2;
	varValue.uiVal = Value;

	HRESULT result = PropertyBag->Write(1, &option, &varValue);
	assert(SUCCEEDED(result));
}

__forceinline void WriteBagPropUI1(IPropertyBag2* PropertyBag, LPOLESTR Name, u8 Value)
{
	PROPBAG2 option = {};
	option.pstrName = Name;

	VARIANT varValue;
	VariantInit(&varValue);
	varValue.vt = VT_UI1;
	varValue.cVal = Value;

	HRESULT result = PropertyBag->Write(1, &option, &varValue);
	assert(SUCCEEDED(result));
}

__forceinline void WriteBagPropBOOL(IPropertyBag2* PropertyBag, LPOLESTR Name, bool Value)
{
	PROPBAG2 option = {};
	option.pstrName = Name;

	VARIANT varValue;
	VariantInit(&varValue);
	varValue.vt = VT_BOOL;
	varValue.boolVal = Value ? VARIANT_TRUE : VARIANT_FALSE;

	HRESULT result = PropertyBag->Write(1, &option, &varValue);
	assert(SUCCEEDED(result));
}

bool HdpEncodeImageRGBA(u8* Data, i32 Width, i32 Height, u8* OutData, i32* OutDataSize)
{
	IWICImagingFactory*		factory = NULL;
	IWICBitmapEncoder*		encoder = NULL;
	IWICBitmapFrameEncode*	bitmapFrame = NULL;
	IPropertyBag2*			propertyBag = NULL;
	IWICStream*				stream = NULL;
	HRESULT					result;

	result = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)&factory);
	assert(SUCCEEDED(result));

	result = factory->CreateStream(&stream);
	assert(SUCCEEDED(result));

	result = stream->InitializeFromMemory(OutData, *OutDataSize);
	assert(SUCCEEDED(result));

	result = factory->CreateEncoder(GUID_ContainerFormatWmp, NULL, &encoder);
	assert(SUCCEEDED(result));

	result = encoder->Initialize(stream, WICBitmapEncoderNoCache);
	assert(SUCCEEDED(result));

	result = encoder->CreateNewFrame(&bitmapFrame, &propertyBag);
	assert(SUCCEEDED(result));

	WriteBagPropBOOL(propertyBag, L"CompressedDomainTranscode", false);
	WriteBagPropR4(propertyBag, L"ImageQuality", 1.0f); // Ignored UCO, Alpha/Quality
	WriteBagPropBOOL(propertyBag, L"UseCodecOptions", true);	
	WriteBagPropUI2(propertyBag, L"HorizontalTileSlices", 1);
	WriteBagPropUI2(propertyBag, L"VerticalTileSlices", 1);
	WriteBagPropBOOL(propertyBag, L"ProgressiveMode", false);

	WriteBagPropUI1(propertyBag, L"Overlap", 2); // Or 1?
	WriteBagPropUI1(propertyBag, L"Quality", 32); // 1 - 255 (1 Lossless)
	WriteBagPropUI1(propertyBag, L"Subsampling", 3); // 3 444, 2 420
	
	WriteBagPropBOOL(propertyBag, L"InterleavedAlpha", false);
	WriteBagPropUI1(propertyBag, L"AlphaDataDiscard", 0); // Ignored CDT
	WriteBagPropUI1(propertyBag, L"AlphaQuality", 64); // 1 - 255 (1 Lossless)

	result = bitmapFrame->Initialize(propertyBag);
	assert(SUCCEEDED(result));

	result = bitmapFrame->SetSize(Width, Height);
	assert(SUCCEEDED(result));

	//GUID_WICPixelFormat24bppBGR
	WICPixelFormatGUID formatGUID = GUID_WICPixelFormat32bppBGRA;
	result = bitmapFrame->SetPixelFormat(&formatGUID);
	assert(SUCCEEDED(result));

	result = IsEqualGUID(formatGUID, GUID_WICPixelFormat32bppBGRA) ? S_OK : E_FAIL;
	assert(SUCCEEDED(result));

	UINT cbStride = Width * 4; // (Width * 24 + 7) / 8;
	UINT cbBufferSize = Height * cbStride;
	BYTE *pbBuffer = new BYTE[cbBufferSize];
	i32 pixelCount = Width * Height;

	for (i32 i = 0; i < pixelCount; i++)
	{
		pbBuffer[i * 4 + 0] = Data[i * 4 + 2];
		pbBuffer[i * 4 + 1] = Data[i * 4 + 1];
		pbBuffer[i * 4 + 2] = Data[i * 4 + 0];
		pbBuffer[i * 4 + 3] = Data[i * 4 + 3];
	}

	result = bitmapFrame->WritePixels(Height, cbStride, cbBufferSize, pbBuffer);

	delete[] pbBuffer;

	assert(SUCCEEDED(result));

	result = bitmapFrame->Commit();
	assert(SUCCEEDED(result));

	result = encoder->Commit();
	assert(SUCCEEDED(result));

	ULARGE_INTEGER streamSize;
	LARGE_INTEGER moveZero = {};
	stream->Seek(moveZero, STREAM_SEEK_CUR, &streamSize);
	*OutDataSize = (i32)streamSize.QuadPart;

	factory->Release();
	bitmapFrame->Release();
	encoder->Release();
	stream->Release();

	return true;
}

bool HdpDecodeImageBGRA(u8* InputData, i32 InputDataSize, u8* OutputData)
{
	IWICImagingFactory*		factory = NULL;
	IWICBitmapDecoder*		decoder = NULL;
	IWICBitmapFrameDecode*	bitmapFrame = NULL;
	IPropertyBag2*			propertyBag = NULL;
	IWICStream*				stream = NULL;
	
	HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)&factory);
	assert(SUCCEEDED(result));

	result = factory->CreateStream(&stream);
	assert(SUCCEEDED(result));

	result = stream->InitializeFromMemory(InputData, InputDataSize);
	assert(SUCCEEDED(result));

	const GUID vendorGUID = GUID_ContainerFormatWmp;
	result = factory->CreateDecoderFromStream(stream, &vendorGUID, WICDecodeMetadataCacheOnDemand, &decoder);
	assert(SUCCEEDED(result));

	result = decoder->GetFrame(0, &bitmapFrame);
	assert(SUCCEEDED(result));

	UINT w, h;
	bitmapFrame->GetSize(&w, &h);
	WICPixelFormatGUID pixelFormat;
	bitmapFrame->GetPixelFormat(&pixelFormat);

	assert(w == 120 && h == 120);

	WICRect srcRect = {};
	srcRect.X = 0;
	srcRect.Y = 0;
	srcRect.Height = h;
	srcRect.Width = w;

	result = bitmapFrame->CopyPixels(&srcRect, 4 * w, 120 * 120 * 4, OutputData);
	assert(SUCCEEDED(result));
	
	factory->Release();
	bitmapFrame->Release();
	decoder->Release();

	return true;
}

void HdpBGRAToRGBABlockStream(u8* InputData, u8* OutputData)
{
	// Assemble block stream.
	i32 horzBlockCount = 128 / 4;
	i32 blockCount = horzBlockCount * horzBlockCount;
	
	// Assemble each block (4x4 neighbouring pixels)
	for (i32 b = 0; b < blockCount; ++b)
	{
		i32 blockOffset = b * 16 * 4;
		i32 blockX = b % horzBlockCount;
		i32 blockY = b / horzBlockCount;
		i32 pixelX = blockX * 4;
		i32 pixelY = blockY * 4;

		for (i32 bY = 0; bY < 4; ++bY)
		{
			for (i32 bX = 0; bX < 4; ++bX)
			{
				i32 pixelIndex = (pixelY + bY) * 128 + (pixelX + bX);
				i32 pixelOffset = pixelIndex * 4;

				i32 blockPixelOffset = (bY * 4 + bX) * 4;
				OutputData[blockOffset + blockPixelOffset + 0] = InputData[pixelOffset + 2];
				OutputData[blockOffset + blockPixelOffset + 1] = InputData[pixelOffset + 1];
				OutputData[blockOffset + blockPixelOffset + 2] = InputData[pixelOffset + 0];
				OutputData[blockOffset + blockPixelOffset + 3] = InputData[pixelOffset + 3];
			}
		}
	}
}