#include "pageBuilder.h"
#include "hdp.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

struct vsTextureDef;

struct vsMipCacheEntry
{
	vsTextureDef*		texture;
	i32					mipLevel;
	u8*					channel0Data;
	u8*					channel1Data;
	i32					imgWidth;
	i32					imgHeight;
	vsMipCacheEntry*	next;
};

struct vsImageDef
{
	vsMipCacheEntry* mipCache[11];
	const char* bcFileName;
	const char* nmFileName;
	const char* mrFileName;
};

struct vsTextureDef
{	
	i32 x;
	i32 y;
	i32 width;
	i32 height;
	vsImageDef* image;
};

const i32 maxMipCacheSize = 32;
const i64 maxMipCacheMemory = 1024LL * 1024LL * 1024LL * 2LL; // 2GB
const i32 maxTextures = 65536;
const i32 maxImages = 65536;

vsImageDef images[maxImages];
i32 imageCount = 0;

vsTextureDef textures[maxTextures];
i32 textureCount = 0;

i16 textureIdTable[1024 * 1024];

vsMipCacheEntry* mipCacheHead = NULL;
i32 mipCacheCount = 0;
i64 mipCacheMemory = 0;

void AddTexture(i32 X, i32 Y, i32 Width, i32 Height, vsImageDef* Image)
{
	if (textureCount >= maxTextures)
	{
		std::cout << "Can't add more textures\n";
		return;
	}

	vsTextureDef tex = {};
	tex.x = X / 128;
	tex.y = Y / 128;
	tex.width = (Width + 127) / 128;
	tex.height = (Height + 127) / 128;
	tex.image = Image;

	textures[textureCount++] = tex;
}

void AddImage(const char* BCFileName, const char* NMFileName, const char* MRFileName)
{
	if (imageCount > maxImages)
	{
		std::cout << "Can't add more images\n";
		return;
	}

	vsImageDef image = {};
	image.bcFileName = BCFileName;
	image.nmFileName = NMFileName;
	image.mrFileName = MRFileName;

	images[imageCount++] = image;
}

vsMipCacheEntry* GetMip(vsTextureDef* Tex, i32 MipLevel)
{
	// NOTE: Mip7 is 1x1 for a 128x128 tile.
	assert(MipLevel <= 7);

	// Find closest mip (only finer)
	i32 closestMip = MipLevel;
	while (closestMip >= 0)
	{
		if (Tex->image->mipCache[closestMip] != NULL)
			break;

		--closestMip;
	}

	vsMipCacheEntry* mipEntry = NULL;

	if (closestMip == MipLevel)
	{
		return Tex->image->mipCache[MipLevel];
	}
	else if (closestMip == -1)
	{
		// NOTE: The image is not loaded at all, therefore we need to load from disk and populate the channel buffers accordingly.
		std::cout << "Building image data: " << Tex->image->bcFileName << " " << Tex->image->nmFileName << "\n";
		
		mipEntry = new vsMipCacheEntry();
		mipEntry->texture = Tex;
		mipEntry->mipLevel = 0;

		mipEntry->channel0Data = CreateImageFromFile(Tex->image->bcFileName, &mipEntry->imgWidth, &mipEntry->imgHeight);
		mipEntry->channel1Data = new u8[mipEntry->imgWidth * mipEntry->imgHeight * 4];		
		memset(mipEntry->channel1Data, 0, mipEntry->imgWidth * mipEntry->imgHeight * 4);

		u8* nmData = CreateImageFromFile(Tex->image->nmFileName, NULL, NULL);
		u8* mrData = CreateImageFromFile(Tex->image->mrFileName, NULL, NULL);

		i32 texelCount = mipEntry->imgWidth * mipEntry->imgHeight;
		for (i32 t = 0; t < texelCount; ++t)
		{
			mipEntry->channel0Data[t * 4 + 3] = nmData[t * 4 + 0];
			mipEntry->channel1Data[t * 4 + 3] = nmData[t * 4 + 1];

			mipEntry->channel1Data[t * 4 + 0] = mrData[t * 4 + 0];
			mipEntry->channel1Data[t * 4 + 1] = mrData[t * 4 + 1];
		}
		
		FreeImage(nmData);
		FreeImage(mrData);

		mipEntry->next = mipCacheHead;
		mipCacheHead = mipEntry;
		mipCacheMemory += mipEntry->imgWidth * mipEntry->imgHeight * 8;
		Tex->image->mipCache[0] = mipEntry;

		closestMip = 0;
	}

	i32 mipSteps = MipLevel - closestMip;
	
	// NOTE: We can discard any higher levels than the mip we request? Becuase we only get smaller?
	// This relies on the page builder building mips in order.

	for (i32 i = 0; i < mipSteps; ++i)
	{
		std::cout << "Mip Resizes " << i << "/" << mipSteps << "\n";

		vsMipCacheEntry* mipSrc = Tex->image->mipCache[closestMip + i];

		mipEntry = new vsMipCacheEntry();
		mipEntry->texture = Tex;
		mipEntry->mipLevel = closestMip + i + 1;
		mipEntry->channel0Data = new u8[mipSrc->imgWidth / 2 * mipSrc->imgHeight / 2 * 4];
		mipEntry->channel1Data = new u8[mipSrc->imgWidth / 2 * mipSrc->imgHeight / 2 * 4];
		mipEntry->imgWidth = mipSrc->imgWidth / 2;
		mipEntry->imgHeight = mipSrc->imgHeight / 2;
		
		stbir_resize_uint8(mipSrc->channel0Data, mipSrc->imgWidth, mipSrc->imgHeight, 0, mipEntry->channel0Data, mipSrc->imgWidth / 2, mipSrc->imgHeight / 2, 0, 4);
		stbir_resize_uint8(mipSrc->channel1Data, mipSrc->imgWidth, mipSrc->imgHeight, 0, mipEntry->channel1Data, mipSrc->imgWidth / 2, mipSrc->imgHeight / 2, 0, 4);
		
		mipCacheMemory += mipEntry->imgWidth * mipEntry->imgHeight * 4;
		mipEntry->next = mipCacheHead;
		mipCacheHead = mipEntry;
		Tex->image->mipCache[closestMip + i + 1] = mipEntry;
	}

	assert(mipEntry->mipLevel == MipLevel);

	return mipEntry;
}

void CopyImageData(u8* SrcData, i32 SrcX, i32 SrcY, i32 SrcWidth, u8* DstData, i32 DstX, i32 DstY, i32 DstWidth, i32 CopyWidth, i32 CopyHeight)
{
	for (i32 r = 0; r < CopyHeight; ++r)
	{
		i32 srcOffset = ((r + SrcY) * SrcWidth + SrcX) * 4;
		i32 dstOffset = ((r + DstY) * DstWidth + DstX) * 4;

		//assert(srcOffset >= 0 && srcOffset < (imgMip->imgWidth * imgMip->imgHeight) * 4);
		//assert(dstOffset >= 0 && dstOffset < 128 * 128 * 4);

		memcpy(DstData + dstOffset, SrcData + srcOffset, CopyWidth * 4);
	}
}

void WritePage(FILE* IndexFile, FILE* PageFile, i64* PageFileOffset, u8* Channel0, i32 Channel0Size, u8* Channel1, i32 Channel1Size)
{	
	i32 metaData[] =
	{
		Channel0Size - XR_META_SIZE,

		*(i32*)(Channel0 + XR_META_RGB_SIZE),
		*(i32*)(Channel0 + XR_META_ALPHA_OFFSET),
		*(i32*)(Channel0 + XR_META_ALPHA_SIZE),

		*(i32*)(Channel1 + XR_META_RGB_SIZE),
		*(i32*)(Channel1 + XR_META_ALPHA_OFFSET),
		*(i32*)(Channel1 + XR_META_ALPHA_SIZE),
	};

	i32 payloadSize = (Channel0Size - XR_META_SIZE) + (Channel1Size - XR_META_SIZE) + sizeof(i32) * 7;
	assert(payloadSize <= 0xFFFF);
	
	i64 indexData = *PageFileOffset | ((i64)payloadSize << 48);
	fwrite(&indexData, sizeof(i64), 1, IndexFile);

	fwrite(metaData, sizeof(metaData), 1, PageFile);
	fwrite(Channel0 + XR_META_SIZE, Channel0Size - XR_META_SIZE, 1, PageFile);
	fwrite(Channel1 + XR_META_SIZE, Channel1Size - XR_META_SIZE, 1, PageFile);
	
	*PageFileOffset += payloadSize;
}

void BuildPages()
{	
	double startTime = GetTime();

	AddImage("rawTextures\\baron_bc.png", "rawTextures\\baron_nm.png", "rawTextures\\baron_mr.png");
	AddImage("rawTextures\\radarDome_bc.png", "rawTextures\\blank4k_nm.png", "rawTextures\\radarDome_mr.png");
	AddImage("rawTextures\\connector_bc.png", "rawTextures\\blank4k_nm.png", "rawTextures\\baron_mr.png");
	AddImage("rawTextures\\capsule_bc.png", "rawTextures\\blank4k_nm.png", "rawTextures\\capsule_mr.png");

	/*
	for (i32 iX = 0; iX < 16; ++iX)
		for (i32 iY = 0; iY < 16; ++iY)
		{
			AddTexture(iX * 4096, iY * 4096, 4096, 4096, &images[2]);
		}
	//*/

	AddTexture(4096 * 16, 0, 4096, 4096, &images[0]);
	//AddTexture(4096 * 17, 0, 4096, 4096, &images[2]);
	AddTexture(4096 * 16, 4096, 4096, 4096, &images[1]);
	AddTexture(4096 * 17, 0, 2048, 2048, &images[3]);

	// TODO: Check for overlaps

	for (i32 i = 0; i < ARRAY_COUNT(textureIdTable); ++i)
	{
		textureIdTable[i] = -1;
	}
	
	for (i32 i = 0; i < textureCount; ++i)
	{
		vsTextureDef* t = &textures[i];
		
		i32 sX = t->x;
		i32 sY = t->y;
		i32 eX = t->x + t->width;
		i32 eY = t->y + t->height;

		for (i32 iY = sY; iY < eY; ++iY)
		{
			for (i32 iX = sX; iX < eX; ++iX)
			{
				textureIdTable[iY * 1024 + iX] = i;
			}
		}
	}

	i32 mipCount = 11;

	u8* compositeChannel0 = new u8[128 * 128 * 4];
	u8* compositeChannel1 = new u8[128 * 128 * 4];
	
	u8* pagePayloadChannel0 = new u8[120 * 120 * 4];
	u8* pagePayloadChannel1 = new u8[120 * 120 * 4];

	u8* pageEncodedChannel0 = new u8[120 * 120 * 4];
	u8* pageEncodedChannel1 = new u8[120 * 120 * 4];

	FILE* pageFile = fopen("pages\\page.dat", "wb");
	FILE* indexFile = fopen("pages\\index.dat", "wb");

	i64 pageFileOffset = 0;

	u8* mipTempChannel0 = new u8[1024 * 1024 * 4];
	u8* mipTempChannel1 = new u8[1024 * 1024 * 4];
	
	memset(mipTempChannel0, 0, 1024 * 1024 * 4);
	memset(mipTempChannel1, 0, 1024 * 1024 * 4);
	
	// Loop Mip Levels
	for (i32 m = 0; m < 8; ++m)
	{
		i32 mipPages = 1 << (mipCount - m - 1);
		i32 mipCoverage = 1024 / mipPages;

		for (i32 iY = 0; iY < mipPages; ++iY)
		{
			for (i32 iX = 0; iX < mipPages; ++iX)
			{
				memset(compositeChannel0, 0, 128 * 128 * 4);
				memset(compositeChannel1, 0, 128 * 128 * 4);

				// Convert page to bounds over texture id table.
				i32 sX = iX * mipCoverage;
				i32 sY = iY * mipCoverage;
				i32 eX = (iX + 1) * mipCoverage;
				i32 eY = (iY + 1) * mipCoverage;

				i32 compositeCount = 0;

				for (i32 mY = sY; mY < eY; ++mY)
				{
					for (i32 mX = sX; mX < eX; ++mX)
					{
						// Get correct mip level
						i32 texIndex = textureIdTable[mY * 1024 + mX];
						
						if (texIndex == -1)
							continue;

						++compositeCount;

						vsTextureDef* t = &textures[texIndex];
						vsMipCacheEntry* imgMip = GetMip(t, m);

						// Source and destination rects in pixels.
						i32 gSX = GetMax(t->x * 128, iX * mipCoverage * 128);
						i32 gEX = GetMin(t->x * 128 + imgMip->imgWidth * mipCoverage, (iX + 1) * mipCoverage * 128);
						i32 gSY = GetMax(t->y * 128, iY * mipCoverage * 128);
						i32 gEY = GetMin(t->y * 128 + imgMip->imgHeight * mipCoverage, (iY + 1) * mipCoverage * 128);

						i32 srcSX = (gSX - (t->x * 128)) / mipCoverage;
						i32 srcEX = (gEX - (t->x * 128)) / mipCoverage;
						i32 srcSY = (gSY - (t->y * 128)) / mipCoverage;
						i32 srcEY = (gEY - (t->y * 128)) / mipCoverage;

						i32 dstSX = (gSX - (iX * mipCoverage * 128)) / mipCoverage;
						i32 dstEX = (gEX - (iX * mipCoverage * 128)) / mipCoverage;
						i32 dstSY = (gSY - (iY * mipCoverage * 128)) / mipCoverage;
						i32 dstEY = (gEY - (iY * mipCoverage * 128)) / mipCoverage;

						// Copy section of data to page
						i32 rows = srcEY - srcSY;
						i32 columns = srcEX - srcSX;

						CopyImageData(imgMip->channel0Data, srcSX, srcSY, imgMip->imgWidth, compositeChannel0, dstSX, dstSY, 128, columns, rows);
						CopyImageData(imgMip->channel1Data, srcSX, srcSY, imgMip->imgWidth, compositeChannel1, dstSX, dstSY, 128, columns, rows);

						/*
						for (i32 row = 0; row < rows; ++row)
						{
							i32 srcOffset = ((row + srcSY) * imgMip->imgWidth + srcSX) * 4;
							i32 dstOffset = ((row + dstSY) * 128 + dstSX) * 4;
							
							assert(srcOffset >= 0 && srcOffset < (imgMip->imgWidth * imgMip->imgHeight) * 4);
							assert(dstOffset >= 0 && dstOffset < 128 * 128 * 4);
							memcpy(pageCompositeData + dstOffset, imgMip->imgData + srcOffset, columns * 4);
						}
						*/

						//std::cout << "Mip: " << m << " X: " << mX << " Y: " << mY << " Cached: " << closestMip << "\n";
					}
				}

				if (compositeCount)
				{
					// On the final level we pack the image to the 1024x1024 temp buffer too.
					if (m == 7)
					{
						CopyImageData(compositeChannel0, 0, 0, 128, mipTempChannel0, iX * 128, iY * 128, 1024, 128, 128);
						CopyImageData(compositeChannel1, 0, 0, 128, mipTempChannel1, iX * 128, iY * 128, 1024, 128, 128);
					}

					// Shrink payload
					stbir_resize_uint8(compositeChannel0, 128, 128, 128 * 4, pagePayloadChannel0, 120, 120, 120 * 4, 4);
					stbir_resize_uint8(compositeChannel1, 128, 128, 128 * 4, pagePayloadChannel1, 120, 120, 120 * 4, 4);

					i32 encodedSizeChannel0 = 120 * 120 * 4;
					HdpEncodeImageRGBA(pagePayloadChannel0, 120, 120, pageEncodedChannel0, &encodedSizeChannel0);

					i32 encodedSizeChannel1 = 120 * 120 * 4;
					HdpEncodeImageRGBA(pagePayloadChannel1, 120, 120, pageEncodedChannel1, &encodedSizeChannel1);

					/*
					char fileName[256];
					sprintf(fileName, "pages\\page_%d_%d_%d.jxr", m, iY, iX);
					FILE* outFile = fopen(fileName, "wb");
					fwrite(pageEncodedChannel1, encodedSizeChannel1, 1, outFile);
					fclose(outFile);
					*/
					
					WritePage(indexFile, pageFile, &pageFileOffset, pageEncodedChannel0, encodedSizeChannel0, pageEncodedChannel1, encodedSizeChannel1);

					//if (iX == 0)
					std::cout << "Page encoded: " << m << " " << iY << " " << iX << "\n";
				}
				else
				{
					i64 indexData = 0;
					fwrite(&indexData, sizeof(i64), 1, indexFile);
				}
			}
		}
	}

	// Finish the remaining mip levels.
	std::cout << "Preparing final mips\n";

	u8* mipImgChannel0[] = 
	{
		new u8[512 * 512 * 4],
		new u8[256 * 256 * 4],
		new u8[128 * 128 * 4],
	};

	u8* mipImgChannel1[] =
	{
		new u8[512 * 512 * 4],
		new u8[256 * 256 * 4],
		new u8[128 * 128 * 4],
	};

	stbir_resize_uint8(mipTempChannel0, 1024, 1024, 1024 * 4, mipImgChannel0[0], 512, 512, 512 * 4, 4);
	stbir_resize_uint8(mipImgChannel0[0], 512, 512, 512 * 4, mipImgChannel0[1], 256, 256, 256 * 4, 4);
	stbir_resize_uint8(mipImgChannel0[1], 256, 256, 256 * 4, mipImgChannel0[2], 128, 128, 128 * 4, 4);

	stbir_resize_uint8(mipTempChannel1, 1024, 1024, 1024 * 4, mipImgChannel1[0], 512, 512, 512 * 4, 4);
	stbir_resize_uint8(mipImgChannel1[0], 512, 512, 512 * 4, mipImgChannel1[1], 256, 256, 256 * 4, 4);
	stbir_resize_uint8(mipImgChannel1[1], 256, 256, 256 * 4, mipImgChannel1[2], 128, 128, 128 * 4, 4);

	std::cout << "Outputting final mips\n";

	for (i32 m = 8; m < 11; ++m)
	{
		i32 mipPages = 1 << (mipCount - m - 1);

		for (i32 iY = 0; iY < mipPages; ++iY)
		{
			for (i32 iX = 0; iX < mipPages; ++iX)
			{
				CopyImageData(mipImgChannel0[m - 8], iX * 128, iY * 128, mipPages * 128, compositeChannel0, 0, 0, 128, 128, 128);
				CopyImageData(mipImgChannel1[m - 8], iX * 128, iY * 128, mipPages * 128, compositeChannel1, 0, 0, 128, 128, 128);

				stbir_resize_uint8(compositeChannel0, 128, 128, 128 * 4, pagePayloadChannel0, 120, 120, 120 * 4, 4);
				stbir_resize_uint8(compositeChannel1, 128, 128, 128 * 4, pagePayloadChannel1, 120, 120, 120 * 4, 4);

				i32 encodedSizeChannel0 = 120 * 120 * 4;
				HdpEncodeImageRGBA(pagePayloadChannel0, 120, 120, pageEncodedChannel0, &encodedSizeChannel0);

				i32 encodedSizeChannel1 = 120 * 120 * 4;
				HdpEncodeImageRGBA(pagePayloadChannel1, 120, 120, pageEncodedChannel1, &encodedSizeChannel1);

				WritePage(indexFile, pageFile, &pageFileOffset, pageEncodedChannel0, encodedSizeChannel0, pageEncodedChannel1, encodedSizeChannel1);

				//if (iX == 0)
				std::cout << "Page encoded: " << m << " " << iY << " " << iX << "\n";
			}
		}
	}

	i32 metaDataSize = XR_META_SIZE;
	fwrite(&metaDataSize, sizeof(i32), 1, indexFile);
	fwrite(pageEncodedChannel0, XR_META_SIZE, 1, indexFile);

	fclose(indexFile);
	fclose(pageFile);

	HdpEncodeImageRGBA("pages\\usageMap.jxr", mipTempChannel0, 1024, 1024);

	std::cout << "Page Builder Complete\n";
	std::cout << "Mip Memory Usage: " << ((double)mipCacheMemory / 1024.0 / 1024.0) << "mb\n";

	startTime = GetTime() - startTime;

	std::cout << "Seconds: " << startTime << "\n";
}