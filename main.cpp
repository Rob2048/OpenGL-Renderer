#include "shared.h"
#include "objLoader.h"
#include "pageBuilder.h"
#include "shaderCompile.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <objbase.h>
#include <gl\gl.h>
#include <gl\glext.h>
#include <gl\wglext.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

#include "lz4\lz4.h"

#include "glm.h"

#include "hdp.h"

int	gWidth;
int gHeight;

struct vsPlatform
{
	HWND			windowHandle;
	HINSTANCE		processHandle;
	int64_t			timeFrequency;
	int64_t			timeCounterStart;
	HANDLE			fileReadThread;
	HANDLE			pageTranscodeThread[12];
	i32				pageTranscodeThreadCount;
};

struct vsGame
{
	bool			running;
};

struct vsOpenGL
{
	const char* oglExtensions;
	const char* wglExtensions;
};

struct vsInput
{
	bool	keyForward = false;
	bool	keyBackward = false;
	bool	keyLeft = false;
	bool	keyRight = false;
	bool	mouseRight = false;
	bool	mouseLeft = false;
	bool	lockedMouse = false;
	int		indirectionUIMipLevel = 0;
	bool	purgeCache = false;
	bool	vtDebug = false;
};

struct vsCamera
{
	vec3	camPos;
	vec2	camRot;
	mat4	view;
	mat4	proj;
	float	verticalFOV;
	float	nearPlane;
	float	farPlane;
};

struct vsIndirectionTable
{
	GLuint texture;
};

struct vsCachePage
{
	i32 hash;
	vsCachePage* nextMapPage;
	vsCachePage* prevLRUPage;
	vsCachePage* nextLRUPage;
	int x;
	int y;
	int mip;
	int cacheX;
	int cacheY;
};

const i32 cachePageMapBucketCount = 4096;

struct vsVirtualTextureCache
{
	int				width;
	int				height;
	int				maxPageCount;
	int				pageCount;
	vsCachePage*	pagesLRUFirst;
	vsCachePage*	pagesLRULast;
	vsCachePage*	cachePageMap[cachePageMapBucketCount];
};

struct vsIndirectionTableEntry
{
	u8 x;
	u8 y;
	u8 mip;
};

struct vsVirtualTexture
{
	int		globalMipCount;
	int		widthPagesCount;
	int		heightPagesCount;
	int		totalPagesCount;
	
	u8*		pageData;
	FILE*	pageFile;
	i64**	pageIndexTable;

	GLuint						indirectionTex;
	GLuint						indirectionPBO;
	vsIndirectionTableEntry*	indirectionData;
	i32							indirectionDataSizeBytes;
	u8*							jpgxrHeader;
	i32							jpgxrHeaderSize;
};

struct vsFeedbackHashNode
{
	int keys[16];
};

struct vsFeedbackBuffer
{	
	GLuint				imageBuffer;
	GLint				clearCompShader;

	GLuint				pixelBuffers[2];
	int					readIndex;
	int					writeIndex;

	vsFeedbackHashNode*	tileHashMap;
	int					tileHashNodeCount;
};

struct ClusterOffsetListEntry
{
	uint32_t	itemOffset;
	uint32_t	lightCount;
};

struct ClusterLightListEntry
{
	vec3 position;
	float radius;
	vec4 color;
};

#define MAX_LIGHTS 1024

struct vsClusteredLighting
{
	uint16_t				tempCellLights[16 * 8 * 24 * 256];
	uint8_t					tempCellLightCount[16 * 8 * 24];

	ClusterOffsetListEntry	offsetList[16 * 8 * 24];
	uint32_t				itemList[16 * 8 * 24 * 256];
	ClusterLightListEntry	lightList[MAX_LIGHTS];

	int		itemListCount;
	int		lightListCount;

	GLuint	offsetListSSB;
	GLuint	itemListSSB;
	GLuint	lightListSSB;
};

struct vsLight
{
	vec3	position;
	vec3	color;
	float	radius;
	int		clusterId;
};

struct vsWorld
{
	vsLight	lights[MAX_LIGHTS];
	int		lightCount;
};

struct vsDebugChar
{
	u8* data;
	i32 width;
	i32 height;
};

struct vsBloom
{
	i32 blurIterations;

	GLuint tempColor;
	GLuint blurColors[12];

	GLuint tempFramebuffer;
	GLuint blurFramebuffers[12];

	GLint blurShaderProgram;
	GLint bloomShaderProgram;

	GLuint lensDirtTex;
};

struct vsAmbientOcclusion
{
	GLuint	colorBuffer;
	GLuint	frameBuffer;
	GLint	shaderProgram;
	GLuint	noiseTex;
	vec3	kernel[64];
	
	GLuint	blurColor;
	GLuint	blurFrameBuffer;
	GLint	blurShaderProgram;

	GLint	compositeShaderProgram;
};

vsDebugChar				debugChars[11];
vsPlatform				platform;
vsGame					game;
vsOpenGL				openGL;
vsInput					input;
vsCamera				camera;
vsVirtualTexture		virtualTexture;
vsVirtualTextureCache	vtCache;
vsFeedbackBuffer		feedbackBuffer;
vsWorld					world;
vsClusteredLighting		clusterData;
vsBloom					bloom;
vsAmbientOcclusion		ssao;

GLuint	hdrFramebuffer;
GLuint	hdrFrameBufferColor;
GLuint	hdrFrameBufferDepthStencil;
GLuint	gBufferNormalsColor;

GLint	transCompShader;
GLuint	transDestTex;
GLuint	transInputSBO;
GLuint	transOutputSBO;

struct vsFileJob
{
	i64 fileOffset;
	u8* data;
	i32 dataSize;
	i32 pageX;
	i32 pageY;
	i32 pageMip;
};

const i32		fileJobMax = 4096;

volatile i32	jobsInFlight = 0;

vsFileJob		fileJobs[fileJobMax] = {};
volatile i32	fileReadProduce = 0;
volatile i32	fileReadConsume = 0;

vsFileJob*		transcodeJobs[fileJobMax] = {};
volatile i32	transcodeLock = 0;
volatile i32	transcodeConsume = 0;
volatile i32	transcodeProduce = 0;

vsFileJob*		uploadJobs[fileJobMax] = {};
volatile i32	uploadLock = 0;
volatile i32	uploadProduce = 0;
volatile i32	uploadConsume = 0;

HANDLE			jobNewRequestSemaphore;
HANDLE			jobFileLoadedSemaphore;

struct vsManagedShader
{
	GLint* shaderProgram;
	vsManagedDependency* dependencies;
	char* shaderFileNames[2];
	bool compShader;
};

vsManagedShader managedShaders[128];
i32 managedShaderCount = 0;
HANDLE watchDirectory;
HANDLE fileChangeNotify;
char fileChangeBuffer[512];
OVERLAPPED fileOverlapped;
double fileLastChange = 0;

struct vsPageIndexEntry
{
	i32 pageSize;
	i64 pageOffset;
};

__forceinline vsPageIndexEntry GetPageIndex(vsVirtualTexture* Vt, i32 X, i32 Y, i32 Mip)
{
	vsPageIndexEntry result;

	i32 pagesInMip = (1 << (Vt->globalMipCount - Mip - 1));
	i64 pageFileData = Vt->pageIndexTable[Mip][Y * pagesInMip + X];

	result.pageSize = pageFileData >> 48;
	result.pageOffset = pageFileData & 0x0000FFFFFFFFFFFF;

	return result;
}

void AddLight(vsWorld* World, vsLight* Light)
{
	assert(World->lightCount < MAX_LIGHTS);

	World->lights[World->lightCount++] = *Light;
}

vec3 ToLinear(vec3 Value)
{
	Value.x = pow(Value.x, 2.2f);
	Value.y = pow(Value.y, 2.2f);
	Value.z = pow(Value.z, 2.2f);

	return Value;
}

struct Plane
{
	vec3 n;
	float d;
	vec3 o;
	
	Plane() : n(vec3Zero), d(0.0f), o(vec3Zero) { }
	Plane(float A, float B, float C, float D, vec3 P) : n(vec3(A, B, C)), d(D), o(P) { }
	
	void operator=(vec4& RHS)
	{
		n.x = RHS.x;
		n.y = RHS.y;
		n.z = RHS.z;
		d = RHS.w;
	}
};

struct AABB
{
	vec3 minExtents;
	vec3 maxExtents;

	AABB() { }
	AABB(vec3 Min, vec3 Max) : minExtents(Min), maxExtents(Max) { }

	bool Contains(vec3 Point)
	{
		if (Point.x >= minExtents.x && Point.x <= maxExtents.x &&
			Point.y >= minExtents.y && Point.y <= maxExtents.y &&
			Point.z >= minExtents.z && Point.z <= maxExtents.z)
		{
			return true;
		}

		return false;
	}
};

#define LOAD_GL_FUNC(Name, Func) (Name = (Func)wglGetProcAddress(#Name))

PFNWGLGETEXTENSIONSSTRINGARBPROC	wglGetExtensionsStringARB = 0;
PFNGLGENBUFFERSPROC					glGenBuffers = 0;
PFNGLBINDBUFFERPROC					glBindBuffer = 0;
PFNGLBUFFERDATAPROC					glBufferData = 0;
PFNGLDELETEBUFFERSPROC				glDeleteBuffers = 0;
PFNGLCREATESHADERPROC				glCreateShader = 0;
PFNGLSHADERSOURCEPROC				glShaderSource = 0;
PFNGLCOMPILESHADERPROC				glCompileShader = 0;
PFNGLGETSHADERIVPROC				glGetShaderiv = 0;
PFNGLGETSHADERINFOLOGPROC			glGetShaderInfoLog = 0;
PFNGLCREATEPROGRAMPROC				glCreateProgram = 0;
PFNGLATTACHSHADERPROC				glAttachShader = 0;
PFNGLLINKPROGRAMPROC				glLinkProgram = 0;
PFNGLUSEPROGRAMPROC					glUseProgram = 0;
PFNGLBINDFRAGDATALOCATIONPROC		glBindFragDataLocation = 0;
PFNGLGETPROGRAMIVPROC				glGetProgramiv = 0;
PFNGLGETPROGRAMINFOLOGPROC			glGetProgramInfoLog = 0;
PFNGLGETATTRIBLOCATIONPROC			glGetAttribLocation = 0;
PFNGLVERTEXATTRIBPOINTERPROC		glVertexAttribPointer = 0;
PFNGLENABLEVERTEXATTRIBARRAYPROC	glEnableVertexAttribArray = 0;
PFNGLGENVERTEXARRAYSPROC			glGenVertexArrays = 0;
PFNGLBINDVERTEXARRAYPROC			glBindVertexArray = 0;
PFNGLDELETESHADERPROC				glDeleteShader = 0;
PFNGLUNIFORM4FPROC					glUniform4f = 0;
PFNGLGENERATEMIPMAPPROC				glGenerateMipmap = 0;
PFNGLACTIVETEXTUREPROC				glActiveTexture = 0;
PFNGLUNIFORMMATRIX4FVPROC			glUniformMatrix4fv = 0;
PFNGLGENFRAMEBUFFERSPROC			glGenFramebuffers = 0;
PFNGLBINDFRAMEBUFFERPROC			glBindFramebuffer = 0;
PFNGLCHECKFRAMEBUFFERSTATUSPROC		glCheckFramebufferStatus = 0;
PFNGLDELETEFRAMEBUFFERSPROC			glDeleteFramebuffers = 0;
PFNGLFRAMEBUFFERTEXTURE2DPROC		glFramebufferTexture2D = 0;
PFNGLGENRENDERBUFFERSPROC			glGenRenderbuffers = 0;
PFNGLBINDRENDERBUFFERPROC			glBindRenderbuffer = 0;
PFNGLRENDERBUFFERSTORAGEPROC		glRenderbufferStorage = 0;
PFNGLFRAMEBUFFERRENDERBUFFERPROC	glFramebufferRenderbuffer = 0;
PFNGLMAPBUFFERPROC					glMapBuffer = 0;
PFNGLUNMAPBUFFERPROC				glUnmapBuffer = 0;
PFNGLCOMPRESSEDTEXIMAGE2DPROC		glCompressedTexImage2D = 0;
PFNGLBUFFERSUBDATAPROC				glBufferSubData = 0;
PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC	glCompressedTexSubImage2D = 0;
PFNGLUNIFORM1IPROC					glUniform1i = 0;
PFNGLUNIFORM2FPROC					glUniform2f = 0;
PFNGLBINDBUFFERBASEPROC				glBindBufferBase = 0;
PFNGLBINDIMAGETEXTUREPROC			glBindImageTexture = 0;
PFNGLGETINTEGERI_VPROC				glGetIntegeri_v = 0;
PFNGLDISPATCHCOMPUTEPROC			glDispatchCompute = 0;
PFNGLMEMORYBARRIERPROC				glMemoryBarrier = 0;
PFNGLCOPYIMAGESUBDATAPROC			glCopyImageSubData = 0;
PFNGLTEXSTORAGE2DPROC				glTexStorage2D = 0;
PFNGLCOMPRESSEDTEXIMAGE3DPROC		glCompressedTexImage3D = 0;
PFNGLCOMPRESSEDTEXSUBIMAGE3DPROC	glCompressedTexSubImage3D = 0;
PFNGLUNIFORM3FPROC					glUniform3f = 0;
PFNGLDRAWBUFFERSPROC				glDrawBuffers = 0;
PFNGLUNIFORM3FVPROC					glUniform3fv = 0;

void LoadGLFunctions()
{
	LOAD_GL_FUNC(glUniform3fv, PFNGLUNIFORM3FVPROC);
	LOAD_GL_FUNC(glDrawBuffers, PFNGLDRAWBUFFERSPROC);
	LOAD_GL_FUNC(glUniform3f, PFNGLUNIFORM3FPROC);
	LOAD_GL_FUNC(glCompressedTexSubImage3D, PFNGLCOMPRESSEDTEXSUBIMAGE3DPROC);
	LOAD_GL_FUNC(glCompressedTexImage3D, PFNGLCOMPRESSEDTEXIMAGE3DPROC);
	LOAD_GL_FUNC(glTexStorage2D, PFNGLTEXSTORAGE2DPROC);
	LOAD_GL_FUNC(glCopyImageSubData, PFNGLCOPYIMAGESUBDATAPROC);
	LOAD_GL_FUNC(glMemoryBarrier, PFNGLMEMORYBARRIERPROC);
	LOAD_GL_FUNC(glDispatchCompute, PFNGLDISPATCHCOMPUTEPROC);
	LOAD_GL_FUNC(glGetIntegeri_v, PFNGLGETINTEGERI_VPROC);
	LOAD_GL_FUNC(glBindImageTexture, PFNGLBINDIMAGETEXTUREPROC);
	LOAD_GL_FUNC(glGenBuffers, PFNGLGENBUFFERSPROC);
	LOAD_GL_FUNC(glBindBuffer, PFNGLBINDBUFFERPROC);
	LOAD_GL_FUNC(glBufferData, PFNGLBUFFERDATAPROC);
	LOAD_GL_FUNC(glDeleteBuffers, PFNGLDELETEBUFFERSPROC);
	LOAD_GL_FUNC(glCreateShader, PFNGLCREATESHADERPROC);
	LOAD_GL_FUNC(glShaderSource, PFNGLSHADERSOURCEPROC);
	LOAD_GL_FUNC(glCompileShader, PFNGLCOMPILESHADERPROC);
	LOAD_GL_FUNC(glGetShaderiv, PFNGLGETSHADERIVPROC);
	LOAD_GL_FUNC(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC);
	LOAD_GL_FUNC(glCreateProgram, PFNGLCREATEPROGRAMPROC);
	LOAD_GL_FUNC(glAttachShader, PFNGLATTACHSHADERPROC);
	LOAD_GL_FUNC(glLinkProgram, PFNGLLINKPROGRAMPROC);
	LOAD_GL_FUNC(glUseProgram, PFNGLUSEPROGRAMPROC);
	LOAD_GL_FUNC(glBindFragDataLocation, PFNGLBINDFRAGDATALOCATIONPROC);
	LOAD_GL_FUNC(glGetProgramiv, PFNGLGETPROGRAMIVPROC);
	LOAD_GL_FUNC(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC);
	LOAD_GL_FUNC(glGetAttribLocation, PFNGLGETATTRIBLOCATIONPROC);
	LOAD_GL_FUNC(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
	LOAD_GL_FUNC(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
	LOAD_GL_FUNC(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
	LOAD_GL_FUNC(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
	LOAD_GL_FUNC(glDeleteShader, PFNGLDELETESHADERPROC);
	LOAD_GL_FUNC(glUniform4f, PFNGLUNIFORM4FPROC);
	LOAD_GL_FUNC(glGenerateMipmap, PFNGLGENERATEMIPMAPPROC);
	LOAD_GL_FUNC(glActiveTexture, PFNGLACTIVETEXTUREPROC);
	LOAD_GL_FUNC(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC);
	LOAD_GL_FUNC(glGenFramebuffers, PFNGLGENFRAMEBUFFERSPROC);
	LOAD_GL_FUNC(glBindFramebuffer, PFNGLBINDFRAMEBUFFERPROC);
	LOAD_GL_FUNC(glCheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC);
	LOAD_GL_FUNC(glDeleteFramebuffers, PFNGLDELETEFRAMEBUFFERSPROC);
	LOAD_GL_FUNC(glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC);
	LOAD_GL_FUNC(glGenRenderbuffers, PFNGLGENRENDERBUFFERSPROC);
	LOAD_GL_FUNC(glBindRenderbuffer, PFNGLBINDRENDERBUFFERPROC);
	LOAD_GL_FUNC(glRenderbufferStorage, PFNGLRENDERBUFFERSTORAGEPROC);
	LOAD_GL_FUNC(glFramebufferRenderbuffer, PFNGLFRAMEBUFFERRENDERBUFFERPROC);
	LOAD_GL_FUNC(glMapBuffer, PFNGLMAPBUFFERPROC);
	LOAD_GL_FUNC(glUnmapBuffer, PFNGLUNMAPBUFFERPROC);
	LOAD_GL_FUNC(glCompressedTexImage2D, PFNGLCOMPRESSEDTEXIMAGE2DPROC);
	LOAD_GL_FUNC(glBufferSubData, PFNGLBUFFERSUBDATAPROC);
	LOAD_GL_FUNC(glCompressedTexSubImage2D, PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC);
	LOAD_GL_FUNC(glUniform1i, PFNGLUNIFORM1IPROC);
	LOAD_GL_FUNC(glUniform2f, PFNGLUNIFORM2FPROC);
	LOAD_GL_FUNC(glBindBufferBase, PFNGLBINDBUFFERBASEPROC);
}

void DrawAABBIM(AABB* Box)
{
	glVertex3f(Box->minExtents.x, Box->minExtents.y, Box->minExtents.z); glVertex3f(Box->maxExtents.x, Box->minExtents.y, Box->minExtents.z);
	glVertex3f(Box->maxExtents.x, Box->minExtents.y, Box->minExtents.z); glVertex3f(Box->maxExtents.x, Box->maxExtents.y, Box->minExtents.z);
	glVertex3f(Box->maxExtents.x, Box->maxExtents.y, Box->minExtents.z); glVertex3f(Box->minExtents.x, Box->maxExtents.y, Box->minExtents.z);
	glVertex3f(Box->minExtents.x, Box->maxExtents.y, Box->minExtents.z); glVertex3f(Box->minExtents.x, Box->minExtents.y, Box->minExtents.z);

	glVertex3f(Box->minExtents.x, Box->minExtents.y, Box->maxExtents.z); glVertex3f(Box->maxExtents.x, Box->minExtents.y, Box->maxExtents.z);
	glVertex3f(Box->maxExtents.x, Box->minExtents.y, Box->maxExtents.z); glVertex3f(Box->maxExtents.x, Box->maxExtents.y, Box->maxExtents.z);
	glVertex3f(Box->maxExtents.x, Box->maxExtents.y, Box->maxExtents.z); glVertex3f(Box->minExtents.x, Box->maxExtents.y, Box->maxExtents.z);
	glVertex3f(Box->minExtents.x, Box->maxExtents.y, Box->maxExtents.z); glVertex3f(Box->minExtents.x, Box->minExtents.y, Box->maxExtents.z);

	glVertex3f(Box->minExtents.x, Box->minExtents.y, Box->minExtents.z); glVertex3f(Box->minExtents.x, Box->minExtents.y, Box->maxExtents.z);
	glVertex3f(Box->maxExtents.x, Box->minExtents.y, Box->minExtents.z); glVertex3f(Box->maxExtents.x, Box->minExtents.y, Box->maxExtents.z);
	glVertex3f(Box->maxExtents.x, Box->maxExtents.y, Box->minExtents.z); glVertex3f(Box->maxExtents.x, Box->maxExtents.y, Box->maxExtents.z);
	glVertex3f(Box->minExtents.x, Box->maxExtents.y, Box->minExtents.z); glVertex3f(Box->minExtents.x, Box->maxExtents.y, Box->maxExtents.z);
}

void DrawSphereIM(vec3 Origin, float Radius, vec3 Color)
{
	int res = 16;
	
	glColor3fv((GLfloat*)&Color);

	for (int i = 0; i < res; ++i)
	{
		vec2 a, b;

		a.x = (sin((glm::pi<float>() * 2.0f) / res * i)) * Radius;
		a.y = (cos((glm::pi<float>() * 2.0f) / res * i)) * Radius;

		b.x = (sin((glm::pi<float>() * 2.0f) / res * (i + 1))) * Radius;
		b.y = (cos((glm::pi<float>() * 2.0f) / res * (i + 1))) * Radius;

		glVertex3f(Origin.x + a.x, Origin.y + a.y, Origin.z);
		glVertex3f(Origin.x + b.x, Origin.y + b.y, Origin.z);
		
		glVertex3f(Origin.x + a.x, Origin.y, Origin.z + a.y);
		glVertex3f(Origin.x + b.x, Origin.y, Origin.z + b.y);

		glVertex3f(Origin.x, Origin.y + a.x, Origin.z + a.y);
		glVertex3f(Origin.x, Origin.y + b.x, Origin.z + b.y);
	}
}

Plane CreatePlane(vec3 P1, vec3 P2, vec3 P3)
{
	Plane result;

	vec3 p01 = P1 - P2;
	vec3 p21 = P3 - P2;
	vec3 pN = glm::cross(p01, p21);
	pN = -glm::normalize(pN);
	// TODO: Don't need to normalize if just checking sides.

	result.o = (P3 - P1) * 0.5f + P1;	
	result.n = pN;
	result.d = (pN.x * P1.x + pN.y * P1.y + pN.z * P1.z);

	return result;
}

i32 GetMin(i32 A, i32 B)
{
	if (A <= B)
		return A;
	else
		return B;
}

i32 GetMax(i32 A, i32 B)
{
	if (A >= B)
		return A;
	else
		return B;
}

void CopyImageData(u8* SrcData, i32 SrcX, i32 SrcY, i32 SrcWidth, u8* DstData, i32 DstX, i32 DstY, i32 DstWidth, i32 CopyWidth, i32 CopyHeight, i32 Channels = 4)
{
	for (i32 r = 0; r < CopyHeight; ++r)
	{
		i32 srcOffset = ((r + SrcY) * SrcWidth + SrcX) * Channels;
		i32 dstOffset = ((r + DstY) * DstWidth + DstX) * Channels;
		memcpy(DstData + dstOffset, SrcData + srcOffset, CopyWidth * Channels);
	}
}

float GetClusterDepthSlice(float Slice)
{
	float eNear = 0.5f;
	float eFar = 10000.0f;
	
	if (Slice == 0)
	{
		// TODO: Use actual near plane
		return 0.01f;
	}

	return eNear * pow((eFar / eNear), ((Slice - 1.0f) / 24.0f));
}

float GetClustedDepthSliceFromPos(float Pos)
{
	float eNear = 0.5f;
	float eFar = 10000.0f;
	return (log((Pos) / eNear) / log(eFar / eNear)) * 24.0f + 1;
}

float ProjectSphereToPlane(vec3 PlaneN, float PlaneD, vec3 SphereO, float SphereR)
{
	float distToPlane = (glm::dot(PlaneN, SphereO) - PlaneD) / glm::length(PlaneN);

	if (distToPlane == 0.0f)
	{
		return SphereR;
	}
	else if (SphereR < distToPlane || SphereR < -distToPlane)
	{
		return -1.0f;
	}
	else
	{
		float c = sqrt(SphereR * SphereR - distToPlane * distToPlane);
		return c;
	}

	return distToPlane;


	float a = (glm::dot(PlaneN, SphereO) + PlaneD);
	float circR = sqrtf(SphereR * SphereR - (a * a) / glm::dot(PlaneN, PlaneN));
	return circR;
}

__forceinline i32 GetVirtualTexturePageHash(i32 X, i32 Y, i32 Mip)
{
	return X * 1000000 + Y * 100 + Mip;
}

vsCachePage* GetCachePage(vsVirtualTextureCache* Cache, int PageHash)
{
	i32 bucketIndex = PageHash % cachePageMapBucketCount;
	vsCachePage* page = Cache->cachePageMap[bucketIndex];

	while (page)
	{
		if (page->hash == PageHash)
			return page;

		page = page->nextMapPage;
	}

	return NULL;
}

vsCachePage* AddCachePage(vsVirtualTextureCache* Cache, i32 X, i32 Y, i32 Mip)
{
	i32 pageHash = GetVirtualTexturePageHash(X, Y, Mip);

	vsCachePage* page = new vsCachePage();

	page->x = X;
	page->y = Y;
	page->mip = Mip;
	page->cacheX = -1;
	page->cacheY = -1;
	page->hash = pageHash;
	page->nextLRUPage = NULL;
	page->prevLRUPage = NULL;
	
	i32 bucketIndex = pageHash % cachePageMapBucketCount;
	
	page->nextMapPage = Cache->cachePageMap[bucketIndex];
	Cache->cachePageMap[bucketIndex] = page;

	return page;
}

bool RemoveCachePage(vsVirtualTextureCache* Cache, vsCachePage* Page)
{
	// TODO: We only remove from hash map at the moment, need to remove from LRU too.
	// Also consider memory cleanup here.

	i32 bucketIndex = Page->hash % cachePageMapBucketCount;
	vsCachePage* tempPage = Cache->cachePageMap[bucketIndex];
	vsCachePage** prevPage = &Cache->cachePageMap[bucketIndex];

	while (tempPage)
	{
		if (tempPage->hash == Page->hash)
		{
			*prevPage = tempPage->nextMapPage;
			return true;
		}
		
		prevPage = &tempPage->nextMapPage;
		tempPage = tempPage->nextMapPage;
	}

	return false;
}

__forceinline i32 GetMipChainTexelCount(i32 TotalMips)
{
	return (i32)(1024.0 * 1024.0 * 1.333333333);
}

__forceinline i32 GetMipChainTexelOffset(i32 Mip, i32 TotalMips, i32 TexelCount)
{
	return (INT32_MAX << ((TotalMips - Mip) * 2)) & TexelCount;
}

__forceinline i32 GetMipWidth(i32 Mip, i32 TotalMips)
{
	// TODO: This only works with square mips.
	return 1 << (TotalMips - Mip - 1);
}

void LoadVirtualTexturePage(vsVirtualTextureCache* Cache, vsVirtualTexture* Vt, i32 X, i32 Y, i32 Mip)
{
	i32 pageHash = GetVirtualTexturePageHash(X, Y, Mip);
	AddCachePage(Cache, X, Y, Mip);

	vsPageIndexEntry pageEntry = GetPageIndex(Vt, X, Y, Mip);
	
	fileJobs[fileReadProduce] = {};
	fileJobs[fileReadProduce].pageX = X;
	fileJobs[fileReadProduce].pageY = Y;
	fileJobs[fileReadProduce].pageMip = Mip;

	if (pageEntry.pageSize == 0)
	{
		fileJobs[fileReadProduce].dataSize = 0;
		fileJobs[fileReadProduce].fileOffset = -1;
	}
	else
	{
		fileJobs[fileReadProduce].dataSize = pageEntry.pageSize;
		fileJobs[fileReadProduce].fileOffset = pageEntry.pageOffset;
	}

	WriteBarrier;
	InterlockedIncrement((volatile long*)&jobsInFlight);
	fileReadProduce = (fileReadProduce + 1) % fileJobMax;
	ReleaseSemaphore(jobNewRequestSemaphore, 1, NULL);
}

void CreateConsole()
{
	AllocConsole();

	freopen("CONIN$", "r", stdin);
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
}

void Quit()
{
	// TODO: Check if something else needs to be done here.
	// Post quite message.
	DestroyWindow(platform.windowHandle);
}

vec2 GetMousePosition()
{
	POINT p;
	GetCursorPos(&p);
	ScreenToClient(platform.windowHandle, &p);
	return vec2((float)p.x, (float)p.y);
}

void SetMousePostiion(vec2 Position)
{
	POINT p;
	p.x = (LONG)Position.x;
	p.y = (LONG)Position.y;
	ClientToScreen(platform.windowHandle, &p);
	SetCursorPos(p.x, p.y);
}

void SetMouseVisibility(bool Show)
{
	ShowCursor(Show);
}

void OutputExtensionList(const char* Extensions)
{
	int extLen = (int)strlen(Extensions) + 1;
	char* extensionsSimple = new char[extLen];

	for (int i = 0; i < extLen; ++i)
	{
		if (Extensions[i] == ' ')
			extensionsSimple[i] = '\n';
		else
			extensionsSimple[i] = Extensions[i];
	}

	std::cout << extensionsSimple << "\n\n";

	delete[] extensionsSimple;
}

bool CompileShader(char* FileName, GLint* Shader, GLenum ShaderType, vsManagedDependency** DepList = NULL)
{	
	i32 fileLen;
	char* shaderData = scompCompileShader(FileName, &fileLen, DepList);

	if (shaderData == NULL)
	{
		std::cout << "Shader file error\n";
		return false;
	}

	*Shader = glCreateShader(ShaderType);
	glShaderSource(*Shader, 1, &shaderData, &fileLen);

	glCompileShader(*Shader);

	GLint status;
	glGetShaderiv(*Shader, GL_COMPILE_STATUS, &status);

	if (status != GL_TRUE)
	{
		char buffer[512];
		glGetShaderInfoLog(*Shader, sizeof(buffer), NULL, buffer);
		std::cout << "Shader Compile Error(" << FileName << "): " << buffer << "\n";
		delete[] shaderData;
		return false;
	}

	delete[] shaderData;

	return true;
}

bool CreateShaderProgram(GLint* Shaders, int ShaderCount, GLint* Program)
{
	*Program = glCreateProgram();

	for (int i = 0; i < ShaderCount; ++i)
	{
		glAttachShader(*Program, Shaders[i]);
	}

	glLinkProgram(*Program);
	
	GLint status;
	glGetProgramiv(*Program, GL_LINK_STATUS, &status);

	if (status != GL_TRUE)
	{
		char buffer[512];
		glGetProgramInfoLog(*Program, sizeof(buffer), NULL, buffer);
		std::cout << "Shader Linking Error: " << buffer << "\n";
		return false;
	}

	return true;
}

bool CreateShaderProgram(char* VertShaderFileName, char* FragShaderFileName, GLint* Program)
{
	GLint shaders[2];
	if (!CompileShader(VertShaderFileName, &shaders[0], GL_VERTEX_SHADER)) return false;
	if (!CompileShader(FragShaderFileName, &shaders[1], GL_FRAGMENT_SHADER)) return false;
	if (!CreateShaderProgram(shaders, 2, Program)) return false;
	glDeleteShader(shaders[0]);
	glDeleteShader(shaders[1]);

	return true;
}

bool RecreateManagedShaderProgram(vsManagedShader* ManagedShader)
{
	// TODO: Delete deps before recreating.
	
	bool success = false;
	GLint shaders[2];
	vsManagedDependency* dep = NULL;

	if (!ManagedShader->compShader)
	{
		if (CompileShader(ManagedShader->shaderFileNames[0], &shaders[0], GL_VERTEX_SHADER, &dep))
		{
			if (CompileShader(ManagedShader->shaderFileNames[1], &shaders[1], GL_FRAGMENT_SHADER, &dep))
			{
				if (CreateShaderProgram(shaders, 2, ManagedShader->shaderProgram))
				{
					ManagedShader->dependencies = dep;
					success = true;
				}

				glDeleteShader(shaders[1]);
			}

			glDeleteShader(shaders[0]);
		}
	}
	else
	{
		if (CompileShader(ManagedShader->shaderFileNames[0], &shaders[0], GL_COMPUTE_SHADER, &dep))
		{
			if (CreateShaderProgram(shaders, 1, ManagedShader->shaderProgram))
			{
				ManagedShader->dependencies = dep;
				success = true;
			}

			glDeleteShader(shaders[1]);
		}
	}

	if (!success)
	{
		// TODO: Delete deps on failure.
	}

	return success;
}

bool CreateManagedShaderProgram(char* VertShaderFileName, char* FragShaderFileName, GLint* Program)
{
	vsManagedShader result = {};
	result.compShader = false;
	result.shaderFileNames[0] = VertShaderFileName;
	result.shaderFileNames[1] = FragShaderFileName;
	result.shaderProgram = Program;

	if (RecreateManagedShaderProgram(&result))
	{
		managedShaders[managedShaderCount++] = result;
		return true;
	}
	
	return false;
}

bool CreateManagedCompShaderProgram(char* CompShaderFileName, GLint* Program)
{
	vsManagedShader result = {};
	result.compShader = true;
	result.shaderFileNames[0] = CompShaderFileName;
	result.shaderProgram = Program;

	if (RecreateManagedShaderProgram(&result))
	{
		managedShaders[managedShaderCount++] = result;
		return true;
	}

	return false;
}

bool CreateComputeShaderProgram(char* FileName, GLint* Program)
{
	GLint computeShader;
	if (!CompileShader(FileName, &computeShader, GL_COMPUTE_SHADER)) return false;
	if (!CreateShaderProgram(&computeShader, 1, Program)) return false;
	glDeleteShader(computeShader);

	return true;
}

uint8_t* LoadDXTFile(const char* FileName, int* Width, int* Height)
{
	int w, h;
	FILE* dxtFile = fopen(FileName, "rb");
	fseek(dxtFile, 12, SEEK_SET);
	fread(&w, 4, 1, dxtFile);
	fread(&h, 4, 1, dxtFile);
	int bytes = w * h;
	uint8_t* dxtData = new uint8_t[bytes];
	fseek(dxtFile, 128, SEEK_SET);
	fread(dxtData, 1, bytes, dxtFile);
	fclose(dxtFile);

	if (Width != NULL) *Width = w;
	if (Height != NULL) *Height = h;

	return dxtData;
}

bool CreateTextureFromFileDXT5(const char* FileName, GLuint* Texture)
{
	// TODO: How do we handle Mipmaps here? Should load directly from file.

	int width, height;
	uint8_t* data = LoadDXTFile(FileName, &width, &height);

	if (data == NULL)
		return false;

	glGenTextures(1, Texture);
	glBindTexture(GL_TEXTURE_2D, *Texture);
	glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, width, height, 0, width * height, data);
	glBindTexture(GL_TEXTURE_2D, 0);

	delete[] data;

	return true;
}

bool CreateHDRTextureFromFile(const char* FileName, GLuint* Texture, bool GenerateMips = true)
{
	i32 hdrX, hdrY, hdrC;
	f32* hdrData = stbi_loadf(FileName, &hdrX, &hdrY, &hdrC, 3);
	
	glGenTextures(1, Texture);
	glBindTexture(GL_TEXTURE_2D, *Texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, hdrX, hdrY, 0, GL_RGB, GL_FLOAT, hdrData);

	if (GenerateMips)
	{
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	glBindTexture(GL_TEXTURE_2D, 0);

	stbi_image_free(hdrData);

	return true;
}

u8* CreateImageFromFile(const char* FileName, i32* Width, i32* Height, i32 PixelType)
{
	int width, height, channels;

	double imgTime = GetTime();
	stbi_uc* data = stbi_load(FileName, &width, &height, &channels, PixelType);
	imgTime = GetTime() - imgTime;

	std::cout << "Loaded " << FileName << " in " << (imgTime * 1000) << "ms\n";

	if (data == NULL)
		return NULL;

	if (Width != NULL)
		*Width = width;

	if (Height != NULL)
		*Height = height;
	
	return data;
}

void FreeImage(u8* ImageData)
{
	stbi_image_free(ImageData);
}

bool CreateTextureFromFile(const char* FileName, GLuint* Texture, bool SRGB = true, bool GenerateMips = true)
{
	int imgWidth, imgHeight, imgChannels;

	double imgTime = GetTime();
	stbi_uc* imgData = stbi_load(FileName, &imgWidth, &imgHeight, &imgChannels, STBI_rgb_alpha);
	imgTime = GetTime() - imgTime;
	std::cout << "Loaded " << FileName << " in " << (imgTime * 1000) << "ms\n";

	if (imgData == NULL)
		return false;

	glGenTextures(1, Texture);
	glBindTexture(GL_TEXTURE_2D, *Texture);

	if (SRGB)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, imgWidth, imgHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, imgData);
	else
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, imgWidth, imgHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, imgData);

	if (GenerateMips)
	{
		glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	glBindTexture(GL_TEXTURE_2D, 0);

	stbi_image_free(imgData);
	
	return true;
}

bool LoadJPEG(const char* FileName)
{
	FILE* file = fopen(FileName, "rb");
	fseek(file, 0, SEEK_END);
	i32 fileLen = ftell(file);
	fseek(file, 0, SEEK_SET);
	u8* buffer = new u8[fileLen];
	fread(buffer, fileLen, 1, file);
	fclose(file);

	int width, height, channels;

	double imgTime = GetTime();
	stbi_uc* imgData = stbi_load_from_memory(buffer, fileLen, &width, &height, &channels, STBI_rgb_alpha);
	imgTime = GetTime() - imgTime;
	std::cout << "Loaded " << FileName << " in " << (imgTime * 1000) << "ms\n";
	stbi_image_free(imgData);

	delete[] buffer;

	return true;
}

bool EncodeHDP(const char* FileName)
{
	int width, height, channels;
	stbi_uc* data = stbi_load(FileName, &width, &height, &channels, STBI_rgb_alpha);
	double hdpTime = GetTime();
	HdpEncodeImageRGBA(FileName, data, width, height);
	hdpTime = GetTime() - hdpTime;
	stbi_image_free(data);
	std::cout << "HDP Encoding " << FileName << " (" << width << "x" << height << ") in " << (hdpTime * 1000.0) << "ms\n";

	return true;
}

u8 *DecodeHDP(const char* FileName)
{
	int width, height;
	
	double hdpTime = GetTime();
	u8* blockData = HdpDecodeImageRGBA(FileName, &width, &height);
	hdpTime = GetTime() - hdpTime;
	
	std::cout << "HDP Decoding " << FileName << " (" << width << "x" << height << ") in " << (hdpTime * 1000.0) << "ms\n";

	return blockData;
}

bool ExportTextureAsDXT()
{
	/*
	// Export DXT
	double dxtTime = GetTime();
	int xBlocks = (imgWidth + 3) / 4;
	int yBlocks = (imgHeight + 3) / 4;
	int dxtSize = xBlocks * yBlocks * 16;
	unsigned char* dxtData = new unsigned char[dxtSize];
	int writtenBlocks = 0;

	for (int iY = 0; iY < yBlocks; ++iY)
	{
	for (int iX = 0; iX < xBlocks; ++iX)
	{
	unsigned char blockData[4 * 4 * 4];

	for (int i = 0; i < 4; ++i)
	memcpy(blockData + i * 16, imgData + (iY * 4 * imgWidth * 4) + (iX * 4 * 4), 16);

	stb_compress_dxt_block(dxtData + writtenBlocks * 16, blockData, 1, STB_DXT_HIGHQUAL);
	}
	}
	dxtTime = GetTime() - dxtTime;
	std::cout << "DXT Time: " << (dxtTime * 1000) << "ms\n";
	*/

	return false;
}

bool CreateModelFromOBJ(const char* FileName, vec4 UVScaleBias, GLuint* VAO, int* IndexCount, vsOBJModel* Model)
{
	vsOBJModel objModel = CreateOBJ(FileName, UVScaleBias);

	if (Model)
	{
		Model->indexCount = objModel.indexCount;
		Model->vertCount = objModel.vertCount;

		Model->indices = new u16[objModel.indexCount];
		Model->verts = new vsVertex[objModel.vertCount];

		memcpy(Model->indices, objModel.indices, sizeof(u16) * objModel.indexCount);
		memcpy(Model->verts, objModel.verts, sizeof(vsVertex) * objModel.vertCount);
	}

	glGenVertexArrays(1, VAO);
	glBindVertexArray(*VAO);

	GLuint vertexBuffer;
	glGenBuffers(1, &vertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vsVertex) * objModel.vertCount, objModel.verts, GL_STATIC_DRAW);

	GLuint indexBuffer;
	glGenBuffers(1, &indexBuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * objModel.indexCount, objModel.indices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vsVertex), 0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vsVertex), (void*)(offsetof(vsVertex, normal)));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vsVertex), (void*)(offsetof(vsVertex, uv)));
	glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(vsVertex), (void*)(offsetof(vsVertex, tangent)));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glEnableVertexAttribArray(3);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

	*IndexCount = objModel.indexCount;

	return true;
}

bool CreateUIQuadModel(GLuint* VAO)
{
	glGenVertexArrays(1, VAO);
	glBindVertexArray(*VAO);

	GLfloat quadVerts[] =
	{
		0.0f, 0.0f,		0.0f, 0.0f,		1.0f, 1.0f, 1.0f,
		0.0f, 1.0f,		0.0f, 1.0f,		1.0f, 1.0f, 1.0f,
		1.0f, 1.0f,		1.0f, 1.0f,		1.0f, 1.0f, 1.0f,

		0.0f, 0.0f,		0.0f, 0.0f,		1.0f, 1.0f, 1.0f,
		1.0f, 1.0f,		1.0f, 1.0f,		1.0f, 1.0f, 1.0f,
		1.0f, 0.0f,		1.0f, 0.0f,		1.0f, 1.0f, 1.0f,
	};

	GLuint uiVertexBuffer;
	glGenBuffers(1, &uiVertexBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, uiVertexBuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * 7, 0);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * 7, (void*)(4 * 2));
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 4 * 7, (void*)(4 * 4));
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);

	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	return true;
}

bool EncodeRGBABlockStreamToDXT5(u8* BlockStream)
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, transInputSBO);
	u8* inputData = (u8*)glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
	assert(inputData);
	memcpy(inputData, BlockStream, 128 * 128 * 4);
	glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

	glUseProgram(transCompShader);	
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, transInputSBO);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, transOutputSBO);

	glDispatchCompute(1, 1, 1);
	GLenum glerr = glGetError();
	glMemoryBarrier(GL_PIXEL_BUFFER_BARRIER_BIT);

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, transOutputSBO);
	glBindTexture(GL_TEXTURE_2D, transDestTex);
	glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 128 * 128, NULL);
	//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	return true;
}

void UpdateIndirectionTable(vsVirtualTexture* Vt, vsCachePage* Page, bool Add)
{	
	i32 pageX = Page->x;
	i32 pageY = Page->y;
	i32 pageMip = Page->mip;
	vsIndirectionTableEntry newEntry;

	if (Add)
	{
		newEntry = { (u8)Page->cacheX, (u8)Page->cacheY, (u8)Page->mip };		
	}
	else
	{
		//assert(pageMip != 10);

		if (pageMip == 10)
			return;
		
		// Sample texel below
		i32 lowerOffset = GetMipChainTexelOffset(pageMip + 1, Vt->globalMipCount, Vt->indirectionDataSizeBytes / sizeof(vsIndirectionTableEntry));
		i32 lowerWidth = GetMipWidth(pageMip + 1, Vt->globalMipCount);
		newEntry = Vt->indirectionData[lowerOffset + (pageY / 2) * lowerWidth + (pageX / 2)];
	}

	i32 offset = GetMipChainTexelOffset(pageMip, Vt->globalMipCount, Vt->indirectionDataSizeBytes / sizeof(vsIndirectionTableEntry));
	i32 width = GetMipWidth(pageMip, Vt->globalMipCount);

	Vt->indirectionData[offset + pageY * width + pageX] = newEntry;

	// Propogate updates through mipchain.
	i32 mipsToUpdate = pageMip;

	// From finest to coarsest.
	for (i32 i = 0; i < mipsToUpdate; ++i)
	{
		i32 mipoffset = GetMipChainTexelOffset(i, Vt->globalMipCount, Vt->indirectionDataSizeBytes / sizeof(vsIndirectionTableEntry));
		i32 mipWidth = GetMipWidth(i, Vt->globalMipCount);

		i32 sX = pageX * (mipWidth / width);
		i32 sY = pageY * (mipWidth / width);
		i32 eX = (pageX + 1) * (mipWidth / width);
		i32 eY = (pageY + 1) * (mipWidth / width);

		for (i32 mX = sX; mX < eX; ++mX)
		{
			for (i32 mY = sY; mY < eY; ++mY)
			{
				i32 mipIndex = mY * mipWidth + mX;

				if (Vt->indirectionData[mipoffset + mipIndex].mip >= pageMip)
				{
					Vt->indirectionData[mipoffset + mipIndex] = newEntry;
				}
			}
		}
	}
}

void ResizeFramebuffers(i32 Width, i32 Height)
{
	std::cout << "Resize " << Width << " " << Height << "\n";

	gWidth = Width;
	gHeight = Height;
	
	if (!game.running)
		return;

	glBindTexture(GL_TEXTURE_2D, hdrFrameBufferColor);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gWidth, gHeight, 0, GL_RGBA, GL_FLOAT, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindTexture(GL_TEXTURE_2D, hdrFrameBufferDepthStencil);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, gWidth, gHeight, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindTexture(GL_TEXTURE_2D, gBufferNormalsColor);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, gWidth, gHeight, 0, GL_RG, GL_FLOAT, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

	// Bloom
	glBindTexture(GL_TEXTURE_2D, bloom.tempColor);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gWidth, gHeight, 0, GL_RGBA, GL_FLOAT, NULL);
	
	i32 blurWidth = gWidth / 2;
	i32 blurHeight = gHeight / 2;

	for (i32 i = 0; i < bloom.blurIterations; ++i)
	{
		glBindTexture(GL_TEXTURE_2D, bloom.blurColors[i * 2 + 0]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, blurWidth, blurHeight, 0, GL_RGBA, GL_FLOAT, NULL);
	
		glBindTexture(GL_TEXTURE_2D, bloom.blurColors[i * 2 + 1]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, blurWidth, blurHeight, 0, GL_RGBA, GL_FLOAT, NULL);
	
		blurWidth /= 2;
		blurHeight /= 2;
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	
	// SSAO
	glBindTexture(GL_TEXTURE_2D, ssao.colorBuffer);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, gWidth / 2, gHeight / 2, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, gWidth / 2, gHeight / 2, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

	glBindTexture(GL_TEXTURE_2D, ssao.blurColor);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, gWidth / 2, gHeight / 2, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);
}

double GetTime()
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	int64_t time = counter.QuadPart - platform.timeCounterStart;
	double result = (double)time / ((double)platform.timeFrequency);

	return result;
}

float GetSinTime(float Duration)
{
	return (float)(sin((GetTime() * glm::pi<float>() * 2.0f) / Duration) * 0.5f + 0.5f);
}

LRESULT CALLBACK WndProc(HWND HWnd, UINT Msg, WPARAM WParam, LPARAM LParam)
{
	switch (Msg)
	{
		case WM_KEYDOWN:
		{
			int32_t key = (int32_t)WParam;

			if (key == 27)
				Quit();

			if (key == 87) input.keyForward = true;
			if (key == 83) input.keyBackward = true;
			if (key == 65) input.keyLeft = true;
			if (key == 68) input.keyRight = true;
						
			if (key == 76) input.vtDebug = !input.vtDebug;

			if (key == 79) input.indirectionUIMipLevel = max(input.indirectionUIMipLevel - 1, 0);
			if (key == 80) input.indirectionUIMipLevel = min(input.indirectionUIMipLevel + 1, 10);
			
			break;
		}

		case WM_KEYUP:
		{
			int32_t key = (int32_t)WParam;

			if (key == 87) input.keyForward = false;
			if (key == 83) input.keyBackward = false;
			if (key == 65) input.keyLeft = false;
			if (key == 68) input.keyRight = false;

			if (key == 75) input.purgeCache = true;

			break;
		}

		case WM_RBUTTONDOWN:
		{
			input.mouseRight = true;
			break;
		}

		case WM_RBUTTONUP:
		{
			input.mouseRight = false;
			break;
		}

		case WM_SIZE:
		{
			ResizeFramebuffers(LOWORD(LParam), HIWORD(LParam));			
			break;
		}

		case WM_DESTROY:
		{
			PostQuitMessage(0);
			break;
		}

		default:
		{
			return DefWindowProc(HWnd, Msg, WParam, LParam);
		}
	}

	return 0;
}

HWND InitializeWindow(HINSTANCE HInstance, int ShowWnd, int Width, int Height, bool Windowed)
{
	RECT wr = { 0, 0, Width, Height };
	AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = NULL;
	wc.cbWndExtra = NULL;
	wc.hInstance = HInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = "MainWindow";
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&wc))
	{	
		//gPlatform.ErrorMessageBox("Error Registering Window Class");
		return 0;
	}

	HWND hwnd = CreateWindowEx(NULL, "MainWindow", "OpenGL Renderer", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, HInstance, NULL);

	if (!hwnd)
	{
		//gPlatform.ErrorMessageBox("Error Creating Window");
		return 0;
	}

	ShowWindow(hwnd, ShowWnd);
	UpdateWindow(hwnd);

	return hwnd;
}

__forceinline float GetRand(float Min, float Max)
{	
	float range = Max - Min;
	float r = (rand() % 100) / 100.0f;
	return Min + range * r;
}

__forceinline float GetRandF()
{
	return float((double)rand() / (double)(RAND_MAX));
}

__forceinline float Lerp(float A, float B, float T)
{
	return A + (B - A) * T;
}

DWORD WINAPI fileReadThreadProc(LPVOID lpParameter)
{
	while (true)
	{
		while (fileReadConsume != fileReadProduce)
		{
			vsFileJob *fileJob = &fileJobs[fileReadConsume];
			double jobTime = GetTime();

			if (fileJob->fileOffset != -1)
			{
				if (virtualTexture.pageData != NULL)
				{
					fileJob->data = virtualTexture.pageData + fileJob->fileOffset + 4;
				}
				else if (virtualTexture.pageFile != NULL)
				{
					_fseeki64(virtualTexture.pageFile, fileJob->fileOffset, SEEK_SET);

					// NOTE: Allocate enough data for final DXT output for both channels (Will always be bigger than xr data).
					//fileJob->data = new u8[virtualTexture.jpgxrHeaderSize + fileJob->dataSize];
					fileJob->data = new u8[128 * 128 * 2];
					fread(fileJob->data, fileJob->dataSize, 1, virtualTexture.pageFile);
				}
			}
			else
			{
				fileJob->data = NULL;
			}

			jobTime = GetTime() - jobTime;
			//std::cout << "Done Job in " << (jobTime * 1000.0) << "ms\n";

			WriteBarrier;
			fileReadConsume = (fileReadConsume + 1) % fileJobMax;

			while (InterlockedCompareExchange((volatile long*)&transcodeLock, 1, 0) == 1);
			transcodeJobs[transcodeProduce] = fileJob;
			transcodeProduce = (transcodeProduce + 1) % fileJobMax;
			InterlockedDecrement((volatile long*)&transcodeLock);

			ReleaseSemaphore(jobFileLoadedSemaphore, 1, NULL);
		}
		
		// Sleepies time.
		WaitForSingleObjectEx(jobNewRequestSemaphore, INFINITE, FALSE);
	}

	return 0;
}

void DecodePackedPage(u8* ChannelData, i32 ChannelSize, u8* OutData, i32 RGBSize, i32 AlphaOffset, i32 AlphaSize, u8* DecodeBuffer, u8* PayloadBuffer)
{
	memcpy(DecodeBuffer + virtualTexture.jpgxrHeaderSize, ChannelData, ChannelSize);

	*(i32*)(&DecodeBuffer[XR_META_RGB_SIZE]) = RGBSize;
	*(i32*)(&DecodeBuffer[XR_META_ALPHA_OFFSET]) = AlphaOffset;
	*(i32*)(&DecodeBuffer[XR_META_ALPHA_SIZE]) = AlphaSize;

	HdpDecodeImageBGRA(DecodeBuffer, virtualTexture.jpgxrHeaderSize + ChannelSize, PayloadBuffer);

	// Create bordered page.
	CopyImageData(PayloadBuffer, 0, 0, 120, OutData, 4, 4, 128, 120, 120, 4);

	// Expand borders
	for (i32 r = 4; r < 124; ++r)
	{
		i32 t = *(i32*)&OutData[(r * 128 + 4) * 4];

		for (i32 c = 0; c < 4; ++c)
		{
			*(i32*)&OutData[(r * 128 + c) * 4] = t;
		}

		t = *(i32*)&OutData[(r * 128 + 123) * 4];

		for (i32 c = 124; c < 128; ++c)
		{
			*(i32*)&OutData[(r * 128 + c) * 4] = t;
		}
	}

	for (i32 r = 0; r < 4; ++r)
	{
		memcpy(&OutData[r * 128 * 4], &OutData[4 * 128 * 4], 128 * 4);
		memcpy(&OutData[(r + 124) * 128 * 4], &OutData[123 * 128 * 4], 128 * 4);
	}
}

DWORD WINAPI PageTranscodeThreadProc(LPVOID lpParameter)
{
	i32 threadNum = (i32)lpParameter;
	
	CoInitializeEx(NULL, COINIT_MULTITHREADED);
	
	u8* bgraBuffer = new u8[128 * 128 * 4];
	u8* blockStreamBuffer = new u8[128 * 128 * 4];
	u8* decodeBuffer = NULL;
	u8* bgraPayloadBuffer = new u8[128 * 128 * 4];

	while (true)
	{
		while (true)
		{
			vsFileJob *fileJob = NULL;

			// Lock access to aquiring next available job.
			while (InterlockedCompareExchange((volatile long*)&transcodeLock, 1, 0) == 1);
			
			if (transcodeConsume != transcodeProduce)
			{
				fileJob = transcodeJobs[transcodeConsume];
				transcodeConsume = (transcodeConsume + 1) % fileJobMax;
				//std::cout << GetTime() << " " << threadNum << " transcode " << fileJob->pageMip << ":" << fileJob->pageX << "," << fileJob->pageY << "\n";
			}
			else
			{
				InterlockedDecrement((volatile long*)&transcodeLock);
				break;
			}

			InterlockedDecrement((volatile long*)&transcodeLock);
			
			double jobTime = GetTime();

			if (!decodeBuffer)
			{
				decodeBuffer = new u8[128 * 128];
				memcpy(decodeBuffer, virtualTexture.jpgxrHeader, virtualTexture.jpgxrHeaderSize);
			}

			if (fileJob->data)
			{
				i32 metaDataSize = sizeof(i32) * 7;
				i32* metaData = (i32*)fileJob->data;
				i32 channel0Size = metaData[0];
				i32 channel1Size = fileJob->dataSize - metaDataSize - channel0Size;

				u8* channel0Data = fileJob->data + metaDataSize;
				u8* channel1Data = fileJob->data + metaDataSize + channel0Size;
				
				i32 channel0RGBSize = metaData[1];
				i32 channel0AlphaOffset = metaData[2];
				i32 channel0AlphaSize = metaData[3];

				i32 channel1RGBSize = metaData[4];
				i32 channel1AlphaOffset = metaData[5];
				i32 channel1AlphaSize = metaData[6];

				DecodePackedPage(channel0Data, channel0Size, bgraBuffer, channel0RGBSize, channel0AlphaOffset, channel0AlphaSize, decodeBuffer, bgraPayloadBuffer);
								
				if (input.vtDebug)
				{
					for (i32 iX = 0; iX < 128; ++iX)
					{
						for (i32 iY = 0; iY < 128; ++iY)
						{
							if (iX == 4 || iX == 123 || iY == 4 || iY == 123)
							{
								bgraBuffer[(iY * 128 + iX) * 4 + 0] = 0;
								bgraBuffer[(iY * 128 + iX) * 4 + 1] = 0;
								bgraBuffer[(iY * 128 + iX) * 4 + 2] = 255;
								bgraBuffer[(iY * 128 + iX) * 4 + 3] = 0;
							}
						}
					}

					char debugPrintStr[16];
					i32 debugPrintStrLen = sprintf(debugPrintStr, "%d %d", fileJob->pageX, fileJob->pageY);
					i32 xMark = 16;
					i32 yMark = 16;

					for (i32 i = 0; i < debugPrintStrLen; ++i)
					{
						char c = debugPrintStr[i];
						i32 debugCharIdx = -1;

						if (c >= '0' && c <= '9')
							debugCharIdx = c - '0';

						if (debugCharIdx != -1)
							CopyImageData(debugChars[debugCharIdx].data, 0, 0, debugChars[debugCharIdx].width, bgraBuffer, xMark, yMark, 128, debugChars[debugCharIdx].width, debugChars[debugCharIdx].height, 4);

						xMark += 8;
					}
				}
				
				HdpBGRAToRGBABlockStream(bgraBuffer, blockStreamBuffer);

				u8* dxtBuffer = new u8[128 * 128 * 2];

				for (i32 i = 0; i < 1024; ++i)
					stb_compress_dxt_block(dxtBuffer + i * 16, blockStreamBuffer + i * 16 * 4, 1, STB_DXT_NORMAL);//STB_DXT_HIGHQUAL
				
				// Channel 2
				DecodePackedPage(channel1Data, channel1Size, bgraBuffer, channel1RGBSize, channel1AlphaOffset, channel1AlphaSize, decodeBuffer, bgraPayloadBuffer);
				HdpBGRAToRGBABlockStream(bgraBuffer, blockStreamBuffer);
				for (i32 i = 0; i < 1024; ++i)
					stb_compress_dxt_block(dxtBuffer + 128 * 128 + i * 16, blockStreamBuffer + i * 16 * 4, 1, STB_DXT_NORMAL);//STB_DXT_HIGHQUAL

				delete[] fileJob->data;
				fileJob->data = dxtBuffer;
			}

			jobTime = GetTime() - jobTime;
			//std::cout << "Done Job in " << (jobTime * 1000.0) << "ms\n";

			while (InterlockedCompareExchange((volatile long*)&uploadLock, 1, 0) == 1);
			uploadJobs[uploadProduce] = fileJob;
			uploadProduce = (uploadProduce + 1) % fileJobMax;
			//std::cout << threadNum << " completed job " << fileJob->pageMip << ":" << fileJob->pageX << "," << fileJob->pageY << "\n";
			InterlockedDecrement((volatile long*)&uploadLock);
		}

		// Sleepies time.
		WaitForSingleObjectEx(jobFileLoadedSemaphore, INFINITE, FALSE);
	}

	return 0;
}

void InitManagedResources()
{
	char cwd[256];
	GetCurrentDirectory(sizeof(cwd), cwd);
	std::cout << "Directory: " << cwd << "\n";
	
	// TODO: We probably want to watch the entire asset tree, but for now we'll just watch the shaders.
	char watchDir[256];
	strcpy(watchDir, cwd);
	strcat(watchDir, "\\shaders");
	//fileChangeNotify = FindFirstChangeNotification(watchDir, TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE);

	watchDirectory = CreateFile(watchDir, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
	DWORD err = GetLastError();
	assert(watchDirectory != INVALID_HANDLE_VALUE);

	fileOverlapped = {};
	BOOL r = ReadDirectoryChangesW(watchDirectory, fileChangeBuffer, sizeof(fileChangeBuffer), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE, NULL, &fileOverlapped, NULL);
	assert(r == TRUE);
}

void UpdateManagedResources()
{
	DWORD bytes = 0;
	BOOL r = GetOverlappedResult(watchDirectory, &fileOverlapped, &bytes, FALSE);

	if (r == TRUE)
	{
		if (GetTime() > fileLastChange + 0.100f)
		{
			if (bytes)
			{
				FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)&fileChangeBuffer;

				static char* filePath = "shaders\\";
				static i32 filePathLen = strlen(filePath);
				char fileName[256] = {};
				strcpy(fileName, filePath);

				while (true)
				{	
					wcstombs(fileName + filePathLen, fni->FileName, fni->FileNameLength / 2);
					//std::cout << "File change: " << fileName << " " << fni->Action << "\n";

					if (fni->NextEntryOffset == 0)
					{
						break;
					}
					else
					{
						fni = (FILE_NOTIFY_INFORMATION*)(((u8*)fni) + fni->NextEntryOffset);
					}
				}

				// NOTE: At the moment we only care about a single file change, it is very unlikely there will be more than 1 at a time.
				// (Unless you copy a bunch of files into the folder)

				for (i32 i = 0; i < managedShaderCount; ++i)
				{
					vsManagedShader* ms = &managedShaders[i];
					vsManagedDependency* dep = ms->dependencies;

					while (dep != NULL)
					{
						if (strcmp(dep->Name, fileName) == 0)
						{	
							if (!ms->compShader)
								std::cout << "Recreating shader: " << ms->shaderFileNames[0] << " - " << ms->shaderFileNames[1] << "\n";
							else 
								std::cout << "Recreating shader: " << ms->shaderFileNames[0] << "\n";

							RecreateManagedShaderProgram(ms);
							break;
						}

						dep = dep->next;
					}
				}

				fileLastChange = GetTime();
			}
		}

		fileOverlapped = {};
		r = ReadDirectoryChangesW(watchDirectory, fileChangeBuffer, sizeof(fileChangeBuffer), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE, NULL, &fileOverlapped, NULL);
		assert(r == TRUE);
	}
}

int WINAPI WinMain(HINSTANCE HInstance, HINSTANCE HPrevInstance, LPSTR LPCmdLine, int NShowCmd)
{
	gWidth = 1280;
	gHeight = 720;
	game.running = false;

	CreateConsole();

	std::cout << "Starting Engine\n";

	platform.processHandle = HInstance;

	LARGE_INTEGER freq;
	LARGE_INTEGER counter;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&counter);
	platform.timeCounterStart = counter.QuadPart;
	platform.timeFrequency = freq.QuadPart;

	if (!(platform.windowHandle = InitializeWindow(HInstance, NShowCmd, gWidth, gHeight, true)))
		return 1;

	/*
	std::cout << "Press any key to start virtual texture page build...\n";
	std::cin.ignore();
	BuildPages();
	std::cin.ignore();
	return 0;
	//*/

	//-----------------------------------------------------------------------------------------------------------
	// Init GL.
	//-----------------------------------------------------------------------------------------------------------
	HDC deviceContext = GetDC(platform.windowHandle);

	PIXELFORMATDESCRIPTOR pixelInfo = {};
	pixelInfo.nSize = sizeof(PIXELFORMATDESCRIPTOR);
	pixelInfo.nVersion = 1;
	pixelInfo.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pixelInfo.iPixelType = PFD_TYPE_RGBA;
	pixelInfo.cColorBits = 24;
	pixelInfo.cDepthBits = 24;
	pixelInfo.cStencilBits = 8;
	pixelInfo.iLayerType = PFD_MAIN_PLANE;
	
	int pixelFormat = ChoosePixelFormat(deviceContext, &pixelInfo);

	BOOL result = SetPixelFormat(deviceContext, pixelFormat, &pixelInfo);
	assert(result);

	if (result == FALSE)
	{
		std::cout << "Could not set pixel format\n";
		return 1;
	}

	HGLRC glContext = wglCreateContext(deviceContext);
	DWORD error = GetLastError();
	wglMakeCurrent(deviceContext, glContext);
	
	const char* oglVersion = (char*)glGetString(GL_VERSION);
	
	GLint majorVersion, minorVersion;
	glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
	glGetIntegerv(GL_MINOR_VERSION, &minorVersion);	

	std::cout << "GL Version: " << oglVersion << " (" << majorVersion << "." << minorVersion << ")\n";

	wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)wglGetProcAddress("wglGetExtensionsStringARB");
	assert(wglGetExtensionsStringARB);

	if (wglGetExtensionsStringARB == NULL)
	{
		std::cout << "Could not find wglGetExtensionsStringARB.\n";
		return 1;
	}
	
	openGL.wglExtensions = wglGetExtensionsStringARB(deviceContext);	
	openGL.oglExtensions = (char*)glGetString(GL_EXTENSIONS);
	
	LoadGLFunctions();

	// TODO: Move me.
	uint8_t* uncompressedPageBuffer = new uint8_t[128 * 128 * 3];

	//-----------------------------------------------------------------------------------------------------------
	// Threading.
	//-----------------------------------------------------------------------------------------------------------	
	platform.pageTranscodeThreadCount = 3;

	jobNewRequestSemaphore = CreateSemaphoreEx(NULL, 0, 1, NULL, 0, SEMAPHORE_ALL_ACCESS);
	jobFileLoadedSemaphore = CreateSemaphoreEx(NULL, 0, platform.pageTranscodeThreadCount, NULL, 0, SEMAPHORE_ALL_ACCESS);
	platform.fileReadThread = CreateThread(0, 0, fileReadThreadProc, NULL, 0, NULL);
	
	for (i32 i = 0; i < platform.pageTranscodeThreadCount; ++i)
	{
		platform.pageTranscodeThread[i] = CreateThread(0, 0, PageTranscodeThreadProc, (void*)i, 0, NULL);
	}

	//-----------------------------------------------------------------------------------------------------------
	// HDR Framebuffer.
	//-----------------------------------------------------------------------------------------------------------
	glGenTextures(1, &hdrFrameBufferColor);
	glBindTexture(GL_TEXTURE_2D, hdrFrameBufferColor);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gWidth, gHeight, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenTextures(1, &hdrFrameBufferDepthStencil);
	glBindTexture(GL_TEXTURE_2D, hdrFrameBufferDepthStencil);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, gWidth, gHeight, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glBindTexture(GL_TEXTURE_2D, 0);

	// TODO: Create refraction/rough buffer.

	glGenTextures(1, &gBufferNormalsColor);
	glBindTexture(GL_TEXTURE_2D, gBufferNormalsColor);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, gWidth, gHeight, 0, GL_RG, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &hdrFramebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, hdrFramebuffer);	
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrFrameBufferColor, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, gBufferNormalsColor, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, hdrFrameBufferDepthStencil, 0);

	GLuint attachments[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers(ARRAY_COUNT(attachments), attachments);
	
	//glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, hdrFrameBufferDepthStencil);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	//-----------------------------------------------------------------------------------------------------------
	// Bloom Setup.
	//-----------------------------------------------------------------------------------------------------------
	bloom = {};
	bloom.blurIterations = 6;

	glGenTextures(1, &bloom.tempColor);
	glBindTexture(GL_TEXTURE_2D, bloom.tempColor);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gWidth, gHeight, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	
	glGenFramebuffers(1, &bloom.tempFramebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, bloom.tempFramebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom.tempColor, 0);

	i32 blurWidth = gWidth / 2;
	i32 blurHeight = gHeight / 2;

	glGenTextures(bloom.blurIterations * 2, bloom.blurColors);
	glGenFramebuffers(bloom.blurIterations * 2, bloom.blurFramebuffers);

	for (i32 i = 0; i < bloom.blurIterations; ++i)
	{	
		glBindTexture(GL_TEXTURE_2D, bloom.blurColors[i * 2 + 0]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, blurWidth, blurHeight, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		
		glBindFramebuffer(GL_FRAMEBUFFER, bloom.blurFramebuffers[i * 2 + 0]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom.blurColors[i * 2 + 0], 0);

		glBindTexture(GL_TEXTURE_2D, bloom.blurColors[i * 2 + 1]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, blurWidth, blurHeight, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

		glBindFramebuffer(GL_FRAMEBUFFER, bloom.blurFramebuffers[i * 2 + 1]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom.blurColors[i * 2 + 1], 0);
		
		blurWidth /= 2;
		blurHeight /= 2;
	}

	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	CreateManagedShaderProgram("shaders\\ui.vert", "shaders\\blur.frag", &bloom.blurShaderProgram);
	CreateManagedShaderProgram("shaders\\ui.vert", "shaders\\bloom.frag", &bloom.bloomShaderProgram);

	CreateTextureFromFile("textures\\lensDirt4.png", &bloom.lensDirtTex);

	//-----------------------------------------------------------------------------------------------------------
	// Ambient Occlusion Setup.
	//-----------------------------------------------------------------------------------------------------------
	ssao = {};

	glGenTextures(1, &ssao.colorBuffer);
	glBindTexture(GL_TEXTURE_2D, ssao.colorBuffer);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, gWidth / 2, gHeight / 2, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, gWidth / 2, gHeight / 2, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &ssao.frameBuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, ssao.frameBuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssao.colorBuffer, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	CreateManagedShaderProgram("shaders\\ssao.vert", "shaders\\ssao.frag", &ssao.shaderProgram);

	for (i32 i = 0; i < 64; ++i)
	{
		vec3 sample(GetRandF() * 2.0f - 1.0f, GetRandF() * 2.0f - 1.0f, GetRandF());
		sample = glm::normalize(sample);
		sample *= GetRandF();
		float scale = i / 64.0f;
		scale = Lerp(0.1f, 1.0f, scale * scale);
		sample *= scale;
		ssao.kernel[i] = sample;
	}

	vec3 ssaoNoise[16];
	for (i32 i = 0; i < 16; ++i)
	{
		ssaoNoise[i] = vec3(GetRandF() * 2.0f - 1.0f, GetRandF() * 2.0f - 1.0f, 0.0f);
	}

	glGenTextures(1, &ssao.noiseTex);
	glBindTexture(GL_TEXTURE_2D, ssao.noiseTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	glGenTextures(1, &ssao.blurColor);
	glBindTexture(GL_TEXTURE_2D, ssao.blurColor);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, gWidth / 2, gHeight / 2, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenFramebuffers(1, &ssao.blurFrameBuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, ssao.blurFrameBuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ssao.blurColor, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	CreateManagedShaderProgram("shaders\\ui.vert", "shaders\\ssao_blur.frag", &ssao.blurShaderProgram);

	CreateManagedShaderProgram("shaders\\ui.vert", "shaders\\ssao_composite.frag", &ssao.compositeShaderProgram);

	//-----------------------------------------------------------------------------------------------------------
	// Load Resources.
	//-----------------------------------------------------------------------------------------------------------
	// Debug Characters.
	for (i32 i = 0; i < 10; ++i)
	{
		char nameBuffer[256];
		sprintf(nameBuffer, "textures\\debugChars\\num%d.png", i);
		debugChars[i].data = CreateImageFromFile(nameBuffer, &debugChars[i].width, &debugChars[i].height);
	}

	debugChars[10].data = CreateImageFromFile("textures\\debugChars\\charDash.png", &debugChars[10].width, &debugChars[10].height);

	// Models.
	GLuint vao0, vao1, uiVAO, baronVAO;
	int indexCount0, indexCount1, baronIndexCount;
	//CreateModelFromOBJ("ShipTest.obj", vec4(4096, 4096, 69632, 4096), &vao0, &indexCount0);
	//CreateModelFromOBJ("ShipTest.obj", vec4(65536, 65536, 0, 0), &vao0, &indexCount0);
	//CreateModelFromOBJ("ShipTest.obj", vec4(131072, 131072, 0, 0), &vao0, &indexCount0);
	vsOBJModel baronModel;
	CreateModelFromOBJ("models\\Baron.obj", vec4(4096, 4096, 4096 * 16, 0), &vao0, &indexCount0, &baronModel);
	//CreateModelFromOBJ("models\\Baron.obj", vec4(65536, 65536, 0, 0), &vao0, &indexCount0);
	CreateModelFromOBJ("models\\Baron.obj", vec4(4096, 4096, 4096 * 16, 0), &baronVAO, &baronIndexCount, NULL);
	//CreateModelFromOBJ("models\\FuelTank.obj", vec4(4096, 4096, 4096 * 16, 4096), &vao1, &indexCount1, NULL);
	CreateModelFromOBJ("models\\FuelTank.obj", vec4(131072.0f, 131072.0f, 0, 0), &vao1, &indexCount1, NULL);
	CreateUIQuadModel(&uiVAO);

	GLuint machineBlockVAO;
	int machineBlockIndexCount;
	CreateModelFromOBJ("models\\machineBlock.obj", vec4(131072.0f, 131072.0f, 0, 0), &machineBlockVAO, &machineBlockIndexCount, NULL);

	GLuint mgmk2VAO;
	int mgmk2IndexCount;
	CreateModelFromOBJ("models\\mgmk2.obj", vec4(131072.0f, 131072.0f, 0, 0), &mgmk2VAO, &mgmk2IndexCount, NULL);

	GLuint planeVAO;
	int planeIndexCount;
	CreateModelFromOBJ("models\\plane.obj", vec4(2048, 2048, 4096 * 17, 0), &planeVAO, &planeIndexCount, NULL);

	GLuint skySphereVAO;
	int skySphereIndexCount;
	CreateModelFromOBJ("models\\skySphere.obj", vec4(2048, 2048, 4096 * 17, 0), &skySphereVAO, &skySphereIndexCount, NULL);

	GLuint sphereVAO;
	int sphereIndexCount;
	CreateModelFromOBJ("models\\sphere.obj", vec4(2048, 2048, 4096 * 17, 0), &sphereVAO, &sphereIndexCount, NULL);
	
	// Shaders.
	GLint svtForwardShaderProgram, uiShaderProgram, feedbackShaderProgram, simpleShaderProgram, forwardShaderProgram;
	CreateManagedShaderProgram("shaders\\virtual_forward_clustered.vert", "shaders\\forward_clustered.frag", &forwardShaderProgram);
	CreateManagedShaderProgram("shaders\\virtual_forward_clustered.vert", "shaders\\virtual_forward_clustered.frag", &svtForwardShaderProgram);

	GLint simpleVFCShaderProgram;
	//CreateShaderProgram("shaders\\virtual_forward_clustered.vert", "shaders\\vfc_simple.frag", &simpleVFCShaderProgram);
	CreateManagedShaderProgram("shaders\\virtual_forward_clustered.vert", "shaders\\vfc_simple.frag", &simpleVFCShaderProgram);

	GLint flatShaderProgram;
	CreateManagedShaderProgram("shaders\\virtual_forward_clustered.vert", "shaders\\flat.frag", &flatShaderProgram);
	
	GLint skySphereShaderProgram;
	CreateManagedShaderProgram("shaders\\virtual_forward_clustered.vert", "shaders\\sky_sphere.frag", &skySphereShaderProgram);

	CreateManagedShaderProgram("shaders\\ui.vert", "shaders\\ui.frag", &uiShaderProgram);
	CreateManagedShaderProgram("shaders\\feedback.vert", "shaders\\feedback.frag", &feedbackShaderProgram);
	CreateManagedShaderProgram("shaders\\simple.vert", "shaders\\simple.frag", &simpleShaderProgram);

	GLint shShaderProgram;
	CreateManagedShaderProgram("shaders\\virtual_forward_clustered.vert", "shaders\\sh_simple.frag", &shShaderProgram);

	GLint tonemapShaderProgram;
	CreateManagedShaderProgram("shaders\\ui.vert", "shaders\\tonemap.frag", &tonemapShaderProgram);
	
	CreateComputeShaderProgram("shaders\\image_compress.comp", &transCompShader);

	GLint shSolveCompShader;
	CreateManagedCompShaderProgram("shaders\\spherical_harmonics_solve.comp", &shSolveCompShader);

	// Textures
	uint8_t* noPageFoundData = LoadDXTFile("textures\\nopagefound_bc.dds", NULL, NULL);

	GLuint envMap;
	CreateHDRTextureFromFile("textures\\Ditch-River_2k.hdr", &envMap);
	//CreateHDRTextureFromFile("textures\\Alexs_Apt_2k.hdr", &envMap);
	////CreateHDRTextureFromFile("textures\\Milkyway_small.hdr", &envMap);

	GLuint irrMap;
	CreateHDRTextureFromFile("textures\\Ditch-River_Env.hdr", &irrMap, false);

	GLuint scratchTexBC, scratchTexNM;
	CreateTextureFromFile("textures\\scratched_metal_bc.png", &scratchTexBC);
	CreateTextureFromFile("textures\\scratched_metal_nm.png", &scratchTexNM, false);

	GLuint machineBlockCurveTex;
	CreateTextureFromFile("textures\\machine_block_curve.png", &machineBlockCurveTex, false);

	GLuint fuelTankCurveTex;
	CreateTextureFromFile("textures\\fueltank_curve.png", &fuelTankCurveTex, false);

	// TODO: Check all texture parameter setups.
	// Should move all filtering to when a texture and unit are bound, IE main loop?
	//glActiveTexture(GL_TEXTURE0);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);	
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);	
	//glBindTexture(GL_TEXTURE_2D, 0);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	//-----------------------------------------------------------------------------------------------------------
	// Spherical Harmonics.
	//-----------------------------------------------------------------------------------------------------------
	GLuint shBufferTex;
	glGenTextures(1, &shBufferTex);
	glBindTexture(GL_TEXTURE_2D, shBufferTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 9, 1, 0, GL_RGBA, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	//-----------------------------------------------------------------------------------------------------------
	// Feedback Buffer Setup.
	//-----------------------------------------------------------------------------------------------------------
	feedbackBuffer = {};
	feedbackBuffer.readIndex = 0;
	feedbackBuffer.writeIndex = 0;

	glGenTextures(1, &feedbackBuffer.imageBuffer);
	glBindTexture(GL_TEXTURE_2D, feedbackBuffer.imageBuffer);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 160, 120, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);

	feedbackBuffer.tileHashNodeCount = 4096;
	feedbackBuffer.tileHashMap = new vsFeedbackHashNode[feedbackBuffer.tileHashNodeCount];

	for (int i = 0; i < feedbackBuffer.tileHashNodeCount; ++i)
		feedbackBuffer.tileHashMap[i].keys[0] = -1;

	// Feedback Buffer system copy setup.
	glGenBuffers(2, feedbackBuffer.pixelBuffers);

	for (int i = 0; i < 2; ++i)
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER, feedbackBuffer.pixelBuffers[i]);
		glBufferData(GL_PIXEL_PACK_BUFFER, 160 * 120 * 4, NULL, GL_STREAM_READ);
	}

	glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

	CreateComputeShaderProgram("shaders\\feedback_clear.comp", &feedbackBuffer.clearCompShader);

	// NOTE: We need to clear the buffer before first time use.
	glUseProgram(feedbackBuffer.clearCompShader);
	glBindImageTexture(2, feedbackBuffer.imageBuffer, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
	glDispatchCompute(160, 120, 1);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	
	//-----------------------------------------------------------------------------------------------------------
	// Virtual Texture Setup.
	//-----------------------------------------------------------------------------------------------------------	
	virtualTexture = {};
	virtualTexture.globalMipCount = 11;
	virtualTexture.widthPagesCount = 1024;
	virtualTexture.heightPagesCount = 1024;
	virtualTexture.totalPagesCount = virtualTexture.widthPagesCount * virtualTexture.heightPagesCount;

	virtualTexture.pageFile = fopen("pages\\page.dat", "rb");

	/*
	fseek(virtualTexture.pageFile, 0, SEEK_END);
	i64 pageFileLen = _ftelli64(virtualTexture.pageFile);
	fseek(virtualTexture.pageFile, 0, SEEK_SET);

	std::cout << "Caching virtual texture: " << ((double)pageFileLen / 1024.0 / 1024.0 / 1024.0) << "gb...\n";
	virtualTexture.pageData = new u8[pageFileLen];

	i64 partSize = pageFileLen / 100;
	i64 remainingSize = pageFileLen - (partSize * 100);

	for (int i = 0; i < 100; ++i)
	{
	fread(virtualTexture.pageData + (i * partSize), partSize, 1, virtualTexture.pageFile);
	std::cout << i << "%\n";
	}

	// Final part
	fread(virtualTexture.pageData + (100 * partSize), remainingSize, 1, virtualTexture.pageFile);

	fclose(virtualTexture.pageFile);
	virtualTexture.pageFile = NULL;

	std::cout << "Caching virtual texture completed\n";
	//*/

	FILE* pageTableFile = fopen("pages\\index.dat", "rb");
	int vstWidth = 131072;
	int pageWidth = 128;
	int mipLevels = 11;

	virtualTexture.pageIndexTable = new i64*[mipLevels];

	for (int i = 0; i < mipLevels; ++i)
	{
		int pages = (1 << (mipLevels - i - 1));
		pages = max(1, pages);

		virtualTexture.pageIndexTable[i] = new int64_t[pages * pages];

		for (int j = 0; j < pages * pages; ++j)
		{
			fread(&virtualTexture.pageIndexTable[i][j], sizeof(int64_t), 1, pageTableFile);
		}
	}

	// JPEGXR Header
	fread(&virtualTexture.jpgxrHeaderSize, sizeof(i32), 1, pageTableFile);
	virtualTexture.jpgxrHeader = new u8[virtualTexture.jpgxrHeaderSize];
	fread(virtualTexture.jpgxrHeader, virtualTexture.jpgxrHeaderSize, 1, pageTableFile);

	fclose(pageTableFile);

	// Page Caches.	
	vtCache.width = 64;
	vtCache.height = 64;
	vtCache.maxPageCount = vtCache.width * vtCache.height;
	vtCache.pagesLRUFirst = NULL;
	vtCache.pagesLRULast = NULL;
	// TODO: Assemble all the pages into a free list.
	memset(vtCache.cachePageMap, 0, sizeof(vsCachePage*) * cachePageMapBucketCount);
	
	GLuint pageCachePBO;
	glGenBuffers(1, &pageCachePBO);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pageCachePBO);
	glBufferData(GL_PIXEL_UNPACK_BUFFER, 128 * 128, NULL, GL_STREAM_DRAW);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	GLuint pageCacheChannel0;
	glGenTextures(1, &pageCacheChannel0);
	glBindTexture(GL_TEXTURE_2D, pageCacheChannel0);
	glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 8192, 8192, 0, 8192 * 8192, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	GLuint pageCacheChannel1;
	glGenTextures(1, &pageCacheChannel1);
	glBindTexture(GL_TEXTURE_2D, pageCacheChannel1);
	glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 8192, 8192, 0, 8192 * 8192, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	// Indirection Buffer.
	glGenTextures(1, &virtualTexture.indirectionTex);
	glBindTexture(GL_TEXTURE_2D, virtualTexture.indirectionTex);

	i32 texelCount = GetMipChainTexelCount(virtualTexture.globalMipCount);
	virtualTexture.indirectionDataSizeBytes = texelCount * 3;
	virtualTexture.indirectionData = new vsIndirectionTableEntry[texelCount];
	
	// TODO: Mip 9 & 10 seem to have infected pixels! WTF!

	i32 currentTexel = 0;
	for (i32 i = 0; i < virtualTexture.globalMipCount; ++i)
	{
		i32 mipSize = GetMipWidth(i, virtualTexture.globalMipCount);

		for (i32 t = 0; t < mipSize * mipSize; ++t)
		{
			virtualTexture.indirectionData[currentTexel++] = { 0, 0, 10 };
		}
	}

	assert(currentTexel == texelCount);

	u8* mipUploadData = (u8*)virtualTexture.indirectionData;

	for (i32 i = 0; i < virtualTexture.globalMipCount; ++i)
	{
		i32 mipSize = GetMipWidth(i, virtualTexture.globalMipCount);
		glTexImage2D(GL_TEXTURE_2D, i, GL_RGB, mipSize, mipSize, 0, GL_RGB, GL_UNSIGNED_BYTE, mipUploadData);
		mipUploadData += mipSize * mipSize * 3;
	}

	// TODO: Move all this filtering to usage binding spot.
	glActiveTexture(GL_TEXTURE0);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// TODO: Check for active texture unit.
	glActiveTexture(GL_TEXTURE2);
	glBindTexture(GL_TEXTURE_2D, virtualTexture.indirectionTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);

	glBindTexture(GL_TEXTURE_2D, 0);

	glGenBuffers(1, &virtualTexture.indirectionPBO);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, virtualTexture.indirectionPBO);
	glBufferData(GL_PIXEL_UNPACK_BUFFER, virtualTexture.indirectionDataSizeBytes, NULL, GL_STREAM_DRAW);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

	/*
	// Load & lock coarse mips.
	for (i32 i = 10; i >= 5; --i)
	{
		i32 mipWidth = GetMipWidth(i, virtualTexture.globalMipCount);

		for (i32 mX = 0; mX < mipWidth; ++mX)
		{
			for (i32 mY = 0; mY < mipWidth; ++mY)
			{
				AddCachePage();
				LoadVirtualTexturePage(&virtualTexture, mX, mY, i);
			}
		}
	}
	*/

	//-----------------------------------------------------------------------------------------------------------
	// Compute Setup.
	//-----------------------------------------------------------------------------------------------------------
	glGenTextures(1, &transDestTex);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, transDestTex);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 128, 128, 0, 128 * 128, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	glGenBuffers(1, &transInputSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, transInputSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 128 * 128 * 4, NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	glGenBuffers(1, &transOutputSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, transOutputSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 128 * 128, NULL, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	GLuint computeDestTex;
	glGenTextures(1, &computeDestTex);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, computeDestTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	int workGroupMax[3];
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &workGroupMax[0]);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &workGroupMax[1]);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &workGroupMax[2]);
	//std::cout << "Max work group count: " << workGroupMax[0] << ", " << workGroupMax[1] << ", " << workGroupMax[2] << "\n";

	int workGroupSize[3];
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &workGroupSize[0]);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &workGroupSize[1]);
	glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &workGroupSize[2]);
	//std::cout << "Max work group size: " << workGroupSize[0] << ", " << workGroupSize[1] << ", " << workGroupSize[2] << "\n";

	int workGroupInv;
	glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &workGroupInv);
	//std::cout << "Max invocations: " << workGroupInv << "\n";

	GLint computeShader;
	CreateComputeShaderProgram("shaders\\image_blend.comp", &computeShader);
	
	//GLuint tileLow, tileHigh, tileSmall;
	//CreateTextureFromFile("tile_high.png", &tileHigh);
	//CreateTextureFromFile("tile_low.png", &tileLow);
	//CreateTextureFromFile("tile_small.png", &tileSmall);

	//int imgWidth, imgHeight, imgChannels;
	//stbi_uc* imgData = stbi_load("tile_high.png", &imgWidth, &imgHeight, &imgChannels, STBI_rgb_alpha);
	//stbi_image_free(imgData);
	//glBufferData(GL_SHADER_STORAGE_BUFFER, 128 * 128 * 4, imgData, GL_DYNAMIC_DRAW);

	//int width, height;
	//uint8_t* dxtData = LoadDXTFile("tile_low.dds", &width, &height);

	/*
	GLuint texBlendSrcSBO;
	glGenBuffers(1, &texBlendSrcSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, texBlendSrcSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 128 * 128, dxtData, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, texBlendSrcSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	delete[] dxtData;

	dxtData = LoadDXTFile("tile_high.dds", &width, &height);

	GLuint texBlendSrc2SBO;
	glGenBuffers(1, &texBlendSrc2SBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, texBlendSrc2SBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, 128 * 128, dxtData, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, texBlendSrc2SBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	delete[] dxtData;

	GLuint texFinalSBO;
	glGenBuffers(1, &texFinalSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, texFinalSBO);
	// TODO: glBufferStorage
	glBufferData(GL_SHADER_STORAGE_BUFFER, 128 * 128, NULL, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, texFinalSBO);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
	*/

	//-----------------------------------------------------------------------------------------------------------
	// Dispatch Compute.
	//-----------------------------------------------------------------------------------------------------------
	/*
	glUseProgram(computeShader);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tileSmall);

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, tileHigh);

	glUniform4f(0, sin(((float)((int)(GetTime() * 1000.0) % 1000) / 1000.0f) * glm::pi<float>() * 2.0f) * 0.5f + 0.5f, 0, 0, 0);

	glBindImageTexture(0, computeDestTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glDispatchCompute(128 / 4, 128 / 4, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, texFinalSBO);
	glBindTexture(GL_TEXTURE_2D, pageCache);
	glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 63 * 128, 63 * 128, 128, 128, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 128 * 128, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	*/

	//u8* blockStream = DecodeHDP("tile_high.jxr");
	//EncodeRGBABlockStreamToDXT5(blockStream);
	// TODO: Free block stream
	// Any other image data that needs to be free.

	//blockStream = DecodeHDP("tile_low.jxr");
	//EncodeRGBABlockStreamToDXT5(blockStream);
	// TODO: Free block stream

	//-----------------------------------------------------------------------------------------------------------
	// Clustered Lighting Setup
	//-----------------------------------------------------------------------------------------------------------
	// Offset List:	
	//	- 64bits per cell (X * Y * Z)
	//	Item Offset 32b
	//	Light Count 8b
	
	// Item List:
	//	- 32bits * 256 * (All occupied cells)
	//	Array of Light Index 12b x ClusterList.LightCount

	// Light List:
	//	Type?
	//	Position
	//	Color
	
	memset(clusterData.offsetList, 0, sizeof(clusterData.offsetList));
	memset(clusterData.itemList, 0, sizeof(clusterData.itemList));
	memset(clusterData.lightList, 0, sizeof(clusterData.lightList));
	
	// Buffer Size: 16 * 8 * 24 * 256 * 2 = 1.5MB
	glGenBuffers(1, &clusterData.offsetListSSB);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterData.offsetListSSB);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(clusterData.offsetList), clusterData.offsetList, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, clusterData.offsetListSSB);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	// Buffer Size: 16 * 8 * 24 * 256 * 4 = 3MB
	glGenBuffers(1, &clusterData.itemListSSB);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterData.itemListSSB);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(clusterData.itemList), clusterData.itemList, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, clusterData.itemListSSB);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	// Buffer Size: 1024 * 32 = 32KB
	glGenBuffers(1, &clusterData.lightListSSB);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterData.lightListSSB);
	glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(clusterData.lightList), clusterData.lightList, GL_DYNAMIC_DRAW);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, clusterData.lightListSSB);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

	//-----------------------------------------------------------------------------------------------------------
	// Game state setup.
	//-----------------------------------------------------------------------------------------------------------
	// Make sure to call this only after all managed resources are registered.
	InitManagedResources();

	// TODO: Should be generated like in main loop.
	camera = {};
	camera.camPos = vec3(0, 3, 5);
	camera.camRot = vec2(0, 0);
	camera.view = mat4();

	input.vtDebug = false;

	// TODO: Move to World setup.
	world.lightCount = 0;

	vsLight light = {};

	//*
	light.position = vec3(-4.0f, 5.0f, 0.0f);
	//light.color = ToLinear(vec3(1.0f, 1.5f, 2.0f)) * 20.0f;
	light.color = ToLinear(vec3(1.0f, 1.5f, 2.0f)) * 20.0f;
	light.radius = 12.0f;
	light.clusterId = -1;
	AddLight(&world, &light);
	//*/

	/*
	light.position = vec3(-8.0f, 0.65f, 3.0f);
	//light.color = ToLinear(vec3(1.0f, 1.5f, 2.0f)) * 20.0f;
	light.color = ToLinear(vec3(2.0f, 1.5f, 1.0f)) * 1.0f;
	light.radius = 1.0f;
	light.clusterId = -1;
	AddLight(&world, &light);
	*/

	/*
	light.position = vec3(0.5f, 3.0f, 0.0f);
	light.color = ToLinear(vec3(1.0f, 1.0f, 1.0f));
	light.radius = 2.0f;
	light.clusterId = -1;
	AddLight(&world, &light);

	light = {};
	light.position = vec3(3.0f, 5.0f, 0.0f);
	light.color = ToLinear(vec3(1.5f, 1.5f, 1.5f));
	light.radius = 8.0f;
	light.clusterId = -1;
	AddLight(&world, &light);

	light = {};
	light.position = vec3(-2.0f, 6.0f, 3.0f);
	light.color = ToLinear(vec3(0.8f, 1.2f, 2.0f)) * 10.0f;
	light.radius = 8.0f;
	light.clusterId = -1;
	AddLight(&world, &light);
	//*/

	//*
	for (int i = 0; i < 100; ++i)
	{
		light = {};
		light.position = vec3(GetRand(-5, 50), GetRand(0, 10), GetRand(0, -200));
		light.color = ToLinear(vec3(GetRand(1.0f, 4.0f), GetRand(1.0f, 4.0f), GetRand(1.0f, 4.0f)));
		light.radius = 10.0f;
		light.clusterId = -1;
		AddLight(&world, &light);
	}
	//*/

	/*
	for (int i = 0; i < 10; ++i)
	{
		for (int j = 0; j < 10; ++j)
		{	
			for (int k = 0; k < 5; ++k)
			{
				light = {};
				light.position = vec3(i * 6 + 0.5f, 2, j * -20 + k * 2.0f);
				light.color = ToLinear(vec3(0.25f, 0.7f, 1.0f));
				light.radius = 2.0f;
				light.clusterId = -1;
				AddLight(&world, &light);
			}
			//mat4 tempModelMat = glm::translate(mat4(), vec3(i * 6, 0, j * -20));			
		}
	}
	//*/

	//-----------------------------------------------------------------------------------------------------------
	// Main Loop.
	//-----------------------------------------------------------------------------------------------------------
	double startTime = GetTime();
	MSG msg = {};
	game.running = true;
	while (game.running)
	{
		double currentTime = GetTime();
		double deltaTime = currentTime - startTime;
		startTime = currentTime;
		//std::cout << (1.0 / deltaTime) << "fps\n";

		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				game.running = false;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		//-----------------------------------------------------------------------------------------------------------
		// Update game state.
		//-----------------------------------------------------------------------------------------------------------
		UpdateManagedResources();

		// Update camera.
		if (input.mouseRight)
		{
			vec2 mouseCenter = vec2(gWidth / 2, gHeight / 2);

			if (!input.lockedMouse)
			{
				input.lockedMouse = true;
				SetMouseVisibility(false);
				SetMousePostiion(mouseCenter);
			}
			else
			{
				vec2 mouseDelta = GetMousePosition() - mouseCenter;
				camera.camRot += mouseDelta * 0.2f;
				SetMousePostiion(mouseCenter);
			}
		}
		else
		{
			if (input.lockedMouse)
			{
				input.lockedMouse = false;
				SetMouseVisibility(true);
			}
		}

		mat4 viewRotMat = glm::rotate(mat4(), glm::radians(camera.camRot.y), vec3Right);
		viewRotMat = glm::rotate(viewRotMat, glm::radians(camera.camRot.x), vec3Up);

		vec3 camMove(0, 0, 0);
		if (input.keyForward) camMove += vec3(vec4(vec3Forward, 0.0f) * viewRotMat);
		if (input.keyBackward) camMove -= vec3(vec4(vec3Forward, 0.0f) * viewRotMat);
		if (input.keyRight) camMove += vec3(vec4(vec3Right, 0.0f) * viewRotMat);
		if (input.keyLeft) camMove -= vec3(vec4(vec3Right, 0.0f) * viewRotMat);

		if (glm::length(camMove) > 0.0f)
		{
			camMove = glm::normalize(camMove);
			float cameraSpeed = 0.25f;
			camera.camPos += camMove * cameraSpeed;
		}

		camera.view = viewRotMat * glm::translate(mat4(), -camera.camPos);

		float pNear = 0.01f;
		float pFar = 1000.0f;

		mat4 proj = glm::perspective(glm::radians(50.0f), (float)gWidth / gHeight, pNear, pFar);
		mat4 invProj = glm::inverse(proj);
		mat4 view = camera.view;
		mat4 model = mat4();// glm::rotate(mat4(), (float)GetTime(), vec3Up);

		world.lights[0].position = vec3(0.5f, 3.0f, sin(GetTime()) * 6.0f);
		world.lights[1].position = vec3(sin(GetTime()) * 20.0f + 20.0f, 3.0f, 5.0f);

		//-----------------------------------------------------------------------------------------------------------
		// Z Pass.
		//-----------------------------------------------------------------------------------------------------------

		// Setting up framebuffer so any following system can draw debug to it.
		//glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glBindFramebuffer(GL_FRAMEBUFFER, hdrFramebuffer);

		glEnable(GL_FRAMEBUFFER_SRGB);
		float clearValue = pow(0.2f, 2.2f);
		glClearColor(clearValue, clearValue, clearValue, 1.0f);
		glViewport(0, 0, gWidth, gHeight);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST);
		glFrontFace(GL_CW);
		glCullFace(GL_BACK);
		glEnable(GL_CULL_FACE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		glDepthFunc(GL_LESS);

		//-----------------------------------------------------------------------------------------------------------
		// Prepare Clustered Lighting.
		//-----------------------------------------------------------------------------------------------------------
		memset(clusterData.tempCellLightCount, 0, sizeof(clusterData.tempCellLightCount));
		memset(clusterData.offsetList, 0, sizeof(clusterData.offsetList));
		clusterData.itemListCount = 0;
		clusterData.lightListCount = 0;

		for (int i = 0; i < world.lightCount; ++i)
		{
			world.lights[i].clusterId = -1;
		}

		mat4 tempProj = glm::perspective(glm::radians(50.0f), (float)gWidth / gHeight, pNear, pFar);
		mat4 tempInvProj = glm::inverse(tempProj);
		mat4 tempView = view;

		vec4 ndcNearPointB = tempInvProj * vec4(-1.0f, -1.0f, -1.0f, 1.0);
		vec4 ndcNearPointT = tempInvProj * vec4(-1.0f, 1.0f, -1.0f, 1.0);

		// NOTE: We swap stuff because we are using an inverted Z.
		ndcNearPointB.z = -ndcNearPointB.z;
		ndcNearPointB.x = -ndcNearPointB.x;
		ndcNearPointT.z = -ndcNearPointT.z;
		ndcNearPointT.x = -ndcNearPointT.x;

		vec3 frustumRay[] =
		{
			glm::normalize(vec3(ndcNearPointB)),
			glm::normalize(vec3(ndcNearPointT)),
			glm::normalize(vec3(ndcNearPointT) * vec3(-1, 1, 1)),
			glm::normalize(vec3(ndcNearPointB) * vec3(-1, 1, 1)),
		};

		vec2 depthSliceScale[24];

		for (int i = 0; i < 24; ++i)
		{
			float depthZ = GetClusterDepthSlice((float)i);

			vec3 cornerMax = frustumRay[1] * (-(depthZ) / (glm::dot(frustumRay[1], vec3(0, 0, -1))));
			vec3 cornerMin = frustumRay[3] * (-(depthZ) / (glm::dot(frustumRay[3], vec3(0, 0, -1))));

			depthSliceScale[i] = (vec2(cornerMax) - vec2(cornerMin));
		}

		double lightClusterTime = GetTime();
		// Per Light
		for (int wl = 0; wl < world.lightCount; ++wl)
		{
			vec3 lightPos = world.lights[wl].position;
			float lightRadius = world.lights[wl].radius;
			uint16_t lightId = wl;
			vec3 lightViewSpace = vec3(tempView * vec4(lightPos, 1.0f));
			lightViewSpace.z = -lightViewSpace.z;

			int lightMinCX = 16;
			int lightMaxCX = 0;
			int lightMinCY = 8;
			int lightMaxCY = 0;
			int lightMinCZ = (int)GetClustedDepthSliceFromPos(max(lightViewSpace.z - lightRadius, 0.5f));
			int lightMaxCZ = (int)GetClustedDepthSliceFromPos(max(lightViewSpace.z + lightRadius, 0.5f));

			//std::cout << "X: " << lightMinCX << "," << lightMaxCX << "Y: " << lightMinCY << "," << lightMaxCY << " Z: " << lightMinCZ << "," << lightMaxCZ << "\n";

			for (int dS = lightMinCZ; dS <= lightMaxCZ; ++dS)
			{
				float depthZ1 = GetClusterDepthSlice((float)dS);
				float depthZ2 = GetClusterDepthSlice((float)(dS + 1));

				float r1 = ProjectSphereToPlane(vec3(0, 0, 1), depthZ1, lightViewSpace, lightRadius);
				float r2 = ProjectSphereToPlane(vec3(0, 0, 1), depthZ2, lightViewSpace, lightRadius);

				// Take the bigger of the 2, or original if both -1
				// Project the biggest to BOTH surrounding Z slices, get real bounds

				if (dS == (int)GetClustedDepthSliceFromPos(lightViewSpace.z))
					r1 = lightRadius;
				else
					r1 = max(r1, r2);

				glColor3f(1, 0, 0);

				if (r1 != -1)
				{
					/*
					for (int i = 0; i < circleRes; ++i)
					{
					vec2 a, b;

					a.x = (sin((glm::pi<float>() * 2.0f) / circleRes * i)) * r1;
					a.y = (cos((glm::pi<float>() * 2.0f) / circleRes * i)) * r1;

					b.x = (sin((glm::pi<float>() * 2.0f) / circleRes * (i + 1))) * r1;
					b.y = (cos((glm::pi<float>() * 2.0f) / circleRes * (i + 1))) * r1;

					glVertex3f(lightViewSpace.x + a.x, lightViewSpace.y + a.y, depthZ1);
					glVertex3f(lightViewSpace.x + b.x, lightViewSpace.y + b.y, depthZ1);
					}
					*/

					// We need to get the max bounds of both depth slices
					// Convert to depth space (-0.5 to 0.5) then shift to (0 to 1)
					vec2 nMinVS = vec2(lightViewSpace) - r1;
					vec2 nMaxVS = vec2(lightViewSpace) + r1;

					// NOTE: Clamping slightily within bounds to prevent overflow in clusters.
					//vec2 minVS0 = glm::clamp(nMinVS / depthSliceScale[dS] + 0.5f, 0.01f, 0.99f);
					//vec2 maxVS0 = glm::clamp(nMaxVS / depthSliceScale[dS] + 0.5f, 0.01f, 0.99f);

					//vec2 minVS1 = glm::clamp(nMinVS / depthSliceScale[dS + 1] + 0.5f, 0.01f, 0.99f);
					//vec2 maxVS1 = glm::clamp(nMaxVS / depthSliceScale[dS + 1] + 0.5f, 0.01f, 0.99f);

					vec2 minVS0 = nMinVS / depthSliceScale[dS] + 0.5f;
					vec2 maxVS0 = nMaxVS / depthSliceScale[dS] + 0.5f;

					vec2 minVS1 = nMinVS / depthSliceScale[dS + 1] + 0.5f;
					vec2 maxVS1 = nMaxVS / depthSliceScale[dS + 1] + 0.5f;

					// TODO: Always a min extent regsitered on left if light is offscreen on left?
					// TODO: Back plane has one extent, front plane has the other
					minVS0.x = min(minVS0.x, minVS1.x);
					minVS0.y = min(minVS0.y, minVS1.y);

					maxVS0.x = max(maxVS0.x, maxVS1.x);
					maxVS0.y = max(maxVS0.y, maxVS1.y);

					int startCellX = (int)(minVS0.x * 16.0f);
					int startCellY = (int)(minVS0.y * 8.0f);
					int endCellX = (int)(maxVS0.x * 16.0f) + 1;
					int endCellY = (int)(maxVS0.y * 8.0f) + 1;

					if (startCellX < 0) startCellX = 0;
					if (startCellX > 16) startCellX = 16;
					if (startCellY < 0) startCellY = 0;
					if (startCellY > 8) startCellY = 8;

					if (endCellX < 0) endCellX = 0;
					if (endCellX > 16) endCellX = 16;
					if (endCellY < 0) endCellY = 0;
					if (endCellY > 8) endCellY = 8;

					/*
					minVS0 = (minVS0 - 0.5f) * depthSliceScale[dS];
					maxVS0 = (maxVS0 - 0.5f) * depthSliceScale[dS];
					glColor3f(0, 1, 1);
					glVertex3f(minVS0.x, minVS0.y, depthZ1); glVertex3f(maxVS0.x, minVS0.y, depthZ1);
					glVertex3f(maxVS0.x, minVS0.y, depthZ1); glVertex3f(maxVS0.x, maxVS0.y, depthZ1);
					glVertex3f(maxVS0.x, maxVS0.y, depthZ1); glVertex3f(minVS0.x, maxVS0.y, depthZ1);
					glVertex3f(minVS0.x, maxVS0.y, depthZ1); glVertex3f(minVS0.x, minVS0.y, depthZ1);
					*/

					for (int cX = startCellX; cX < endCellX; ++cX)
					{
						for (int cY = startCellY; cY < endCellY; ++cY)
						{
							// Add light to cluster
							int cellIdx = dS * (16 * 8) + cY * 16 + cX;
							assert(cellIdx < (16 * 8 * 24));
							int lightIdxInCell = clusterData.tempCellLightCount[cellIdx];
							assert(lightIdxInCell < 256);

							clusterData.tempCellLights[cellIdx * 256 + lightIdxInCell] = lightId;
							++clusterData.tempCellLightCount[cellIdx];

							//offsetList[dS * (16 * 8) + cY * 16 + cX].itemOffset = 0;
							//offsetList[dS * (16 * 8) + cY * 16 + cX].lightCount = 1;

							/*
							vec2 cellMin(cX * 1.0f / 16.0f, cY * 1.0f / 8.0f);
							vec2 cellMax((cX + 1) * (1.0f / 16.0f), (cY + 1) * 1.0f / 8.0f);

							cellMin = (cellMin - 0.5f) * depthSliceScale[dS];
							cellMax = (cellMax - 0.5f) * depthSliceScale[dS];

							glColor3f(0, 1, 0);
							glVertex3f(cellMin.x, cellMin.y, depthZ1); glVertex3f(cellMax.x, cellMin.y, depthZ1);
							glVertex3f(cellMax.x, cellMin.y, depthZ1); glVertex3f(cellMax.x, cellMax.y, depthZ1);
							glVertex3f(cellMax.x, cellMax.y, depthZ1); glVertex3f(cellMin.x, cellMax.y, depthZ1);
							glVertex3f(cellMin.x, cellMax.y, depthZ1); glVertex3f(cellMin.x, cellMin.y, depthZ1);
							*/
						}
					}
				}
			}
		}

		int highestLightsInCell = 0;

		for (int i = 0; i < 16 * 8 * 24; ++i)
		{
			int cellLightCount = clusterData.tempCellLightCount[i];

			if (cellLightCount > 0)
			{
				clusterData.offsetList[i].lightCount = cellLightCount;
				clusterData.offsetList[i].itemOffset = clusterData.itemListCount;

				if (cellLightCount > highestLightsInCell)
					highestLightsInCell = cellLightCount;

				for (int j = 0; j < cellLightCount; ++j)
				{
					vsLight* worldLight = &world.lights[clusterData.tempCellLights[i * 256 + j]];

					if (worldLight->clusterId == -1)
					{
						clusterData.lightList[clusterData.lightListCount].position = worldLight->position;
						clusterData.lightList[clusterData.lightListCount].radius = worldLight->radius;
						clusterData.lightList[clusterData.lightListCount].color = vec4(worldLight->color, 0);

						worldLight->clusterId = clusterData.lightListCount++;
					}

					clusterData.itemList[clusterData.itemListCount++] = worldLight->clusterId;
				}
			}
		}

		lightClusterTime = GetTime() - lightClusterTime;
		//std::cout << "Item Count: " << clusterData.itemListCount << " Light Count: " << clusterData.lightListCount << " Highest: " << highestLightsInCell << " " << (lightClusterTime * 1000) << "ms\n";

		// Upload cluster info
		// TODO: Play nice with GPU sync.
		// TODO: Use glMapBufferRange

		// Upload entire offset list.
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterData.offsetListSSB);
		void* uploadData = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
		memcpy(uploadData, clusterData.offsetList, sizeof(clusterData.offsetList));
		glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);

		// Upload max item list.
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterData.itemListSSB);
		uploadData = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
		memcpy(uploadData, clusterData.itemList, sizeof(uint32_t) * clusterData.itemListCount);
		glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		// Upload max light list.
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, clusterData.lightListSSB);
		uploadData = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_WRITE_ONLY);
		memcpy(uploadData, clusterData.lightList, sizeof(ClusterLightListEntry) * clusterData.lightListCount);
		glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

		// Debug
		glUseProgram(simpleShaderProgram);
		glUniformMatrix4fv(0, 1, GL_FALSE, (float*)&proj);
		glUniformMatrix4fv(1, 1, GL_FALSE, (float*)&view);
		model = mat4();
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&model);

		glBegin(GL_LINES);

		/*
		for (int i = 0; i < world.lightCount; ++i)
			DrawSphereIM(world.lights[i].position, world.lights[i].radius, world.lights[i].color * 0.1f);
		//*/

		/*
		// Center Mark/Camera
		float markerSize = 0.1f;
		glColor3f(1.0, 1.0, 1.0);
		glVertex3f(0 - markerSize, 0, 0);
		glColor3f(1.0, 0.0, 0.0);
		glVertex3f(0 + markerSize, 0, 0);

		glColor3f(1.0, 1.0, 1.0);
		glVertex3f(0, 0 - markerSize, 0);
		glColor3f(0.0, 1.0, 0.0);
		glVertex3f(0, 0 + markerSize, 0);

		// Light Marker
		glColor3f(0, 0, 0);
		glVertex3f(lightPos.x - markerSize, lightPos.y, lightPos.z);
		glVertex3f(lightPos.x + markerSize, lightPos.y, lightPos.z);

		glVertex3f(lightPos.x, lightPos.y - markerSize, lightPos.z);
		glVertex3f(lightPos.x, lightPos.y + markerSize, lightPos.z);

		AABB actualLightBounds(lightPos - lightRadius, lightPos + lightRadius);
		DrawAABBIM(&actualLightBounds);

		glColor3f(1.0, 1.0, 1.0);
		glVertex3f(lightViewSpace.x - markerSize, lightViewSpace.y, lightViewSpace.z);
		glVertex3f(lightViewSpace.x + markerSize, lightViewSpace.y, lightViewSpace.z);

		glVertex3f(lightViewSpace.x, lightViewSpace.y - markerSize, lightViewSpace.z);
		glVertex3f(lightViewSpace.x, lightViewSpace.y + markerSize, lightViewSpace.z);

		AABB lightBounds(lightViewSpace - lightRadius, lightViewSpace + lightRadius);
		DrawAABBIM(&lightBounds);

		int circleRes = 16;

		for (int i = 0; i < circleRes; ++i)
		{
		vec2 a, b;

		a.x = (sin((glm::pi<float>() * 2.0f) / circleRes * i)) * lightRadius;
		a.y = (cos((glm::pi<float>() * 2.0f) / circleRes * i)) * lightRadius;

		b.x = (sin((glm::pi<float>() * 2.0f) / circleRes * (i + 1))) * lightRadius;
		b.y = (cos((glm::pi<float>() * 2.0f) / circleRes * (i + 1))) * lightRadius;

		glVertex3f(lightViewSpace.x + a.x, lightViewSpace.y, lightViewSpace.z + a.y);
		glVertex3f(lightViewSpace.x + b.x, lightViewSpace.y, lightViewSpace.z + b.y);

		glVertex3f(lightViewSpace.x + a.x, lightViewSpace.y + a.y, lightViewSpace.z);
		glVertex3f(lightViewSpace.x + b.x, lightViewSpace.y + b.y, lightViewSpace.z);

		glVertex3f(lightViewSpace.x, lightViewSpace.y + a.x, lightViewSpace.z + a.y);
		glVertex3f(lightViewSpace.x, lightViewSpace.y + b.x, lightViewSpace.z + b.y);
		}

		glColor3f(1, 0, 1);
		for (int i = 0; i < 4; ++i)
		{
		glVertex3f(0, 0, 0);
		glVertex3f(frustumRay[i].x * pFar, frustumRay[i].y * pFar, frustumRay[i].z * pFar);
		}

		// NDC Bounds
		glColor3f(0.0f, 0.0f, 1.0f);
		glVertex3f(-1, -1, 0); glVertex3f(1, -1, 0);
		glVertex3f(1, -1, 0); glVertex3f(1, 1, 0);
		glVertex3f(1, 1, 0); glVertex3f(-1, 1, 0);
		glVertex3f(-1, 1, 0); glVertex3f(-1, -1, 0);

		for (int i = 0; i < 24; ++i)
		{
		float depthZ = GetClusterDepthSlice(i);

		vec3 corner[] =
		{
		frustumRay[0] * (-(depthZ) / (glm::dot(frustumRay[0], vec3(0, 0, -1)))),
		frustumRay[1] * (-(depthZ) / (glm::dot(frustumRay[1], vec3(0, 0, -1)))),
		frustumRay[2] * (-(depthZ) / (glm::dot(frustumRay[2], vec3(0, 0, -1)))),
		frustumRay[3] * (-(depthZ) / (glm::dot(frustumRay[3], vec3(0, 0, -1)))),
		};

		glVertex3fv((GLfloat*)&corner[0]); glVertex3fv((GLfloat*)&corner[1]);
		glVertex3fv((GLfloat*)&corner[1]); glVertex3fv((GLfloat*)&corner[2]);
		glVertex3fv((GLfloat*)&corner[2]); glVertex3fv((GLfloat*)&corner[3]);
		glVertex3fv((GLfloat*)&corner[3]); glVertex3fv((GLfloat*)&corner[0]);
		}

		vec3 cfNearPoints[] =
		{
		frustumRay[0] * (-(0.5f) / (glm::dot(frustumRay[0], vec3(0, 0, -1)))),
		frustumRay[1] * (-(0.5f) / (glm::dot(frustumRay[1], vec3(0, 0, -1)))),
		frustumRay[2] * (-(0.5f) / (glm::dot(frustumRay[2], vec3(0, 0, -1)))),
		frustumRay[3] * (-(0.5f) / (glm::dot(frustumRay[3], vec3(0, 0, -1)))),
		};

		vec3 cfFarPoints[] =
		{
		frustumRay[0] * (-(10000.0f) / (glm::dot(frustumRay[0], vec3(0, 0, -1)))),
		frustumRay[1] * (-(10000.0f) / (glm::dot(frustumRay[1], vec3(0, 0, -1)))),
		frustumRay[2] * (-(10000.0f) / (glm::dot(frustumRay[2], vec3(0, 0, -1)))),
		frustumRay[3] * (-(10000.0f) / (glm::dot(frustumRay[3], vec3(0, 0, -1)))),
		};

		float sNearX = (cfNearPoints[0].x - cfNearPoints[3].x) / 16.0f;
		float sNearY = (cfNearPoints[0].y - cfNearPoints[1].y) / 8.0f;

		float sFarX = (cfFarPoints[0].x - cfFarPoints[3].x) / 16.0f;
		float sFarY = (cfFarPoints[0].y - cfFarPoints[1].y) / 8.0f;

		glColor3f(0.25f, 0.5f, 1.0f);
		for (int i = 0; i < 17; ++i)
		{
		glVertex3f(cfNearPoints[0].x - i * sNearX, cfNearPoints[0].y, cfNearPoints[0].z);
		glVertex3f(cfNearPoints[0].x - i * sNearX, cfNearPoints[1].y, cfNearPoints[0].z);
		}

		for (int i = 0; i < 9; ++i)
		{
		glVertex3f(cfNearPoints[0].x, cfNearPoints[0].y - i * sNearY, cfNearPoints[0].z);
		glVertex3f(cfNearPoints[3].x, cfNearPoints[0].y - i * sNearY, cfNearPoints[0].z);
		}
		*/

		glEnd();

		// Debug
		/*
		glUseProgram(simpleShaderProgram);
		glUniformMatrix4fv(0, 1, GL_FALSE, (float*)&proj);
		glUniformMatrix4fv(1, 1, GL_FALSE, (float*)&view);
		model = mat4();
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&model);

		glBegin(GL_LINES);

		for (i32 i = 0; i < baronModel.vertCount; ++i)
		{
			vec3 origin = baronModel.verts[i].pos;
			vec3 normal = origin + baronModel.verts[i].normal * 0.05f;
			vec3 tangent = origin + vec3(baronModel.verts[i].tangent) * 0.05f;
			vec3 bitangent = origin + glm::normalize(glm::cross(baronModel.verts[i].normal, vec3(baronModel.verts[i].tangent))) * baronModel.verts[i].tangent.w * 0.05f;

			glColor3f(1, 0, 0);
			glVertex3fv((GLfloat*)&origin);
			glVertex3fv((GLfloat*)&normal);

			glColor3f(0, 1, 0);
			glVertex3fv((GLfloat*)&origin);
			glVertex3fv((GLfloat*)&tangent);

			glColor3f(0, 0, 1);
			glVertex3fv((GLfloat*)&origin);
			glVertex3fv((GLfloat*)&bitangent);
		}

		glEnd();
		//*/

		//-----------------------------------------------------------------------------------------------------------
		// Upload Pages.
		//-----------------------------------------------------------------------------------------------------------
		bool updatedPageCache = false;
		i32 pagesToUploadMax = 16;

		if (input.purgeCache)
		{
			// Purge cache pages;
			for (i32 i = 0; i < cachePageMapBucketCount; ++i)
			{
				vsCachePage* page = vtCache.cachePageMap[i];

				while (page != NULL)
				{
					vsCachePage* tempPage = page;
					page = page->nextMapPage;
					delete[] tempPage;
				}

				vtCache.cachePageMap[i] = NULL;
			}

			vtCache.pageCount = 0;
			vtCache.pagesLRUFirst = NULL;
			vtCache.pagesLRULast = NULL;

			// Purge jobs.
			while (uploadConsume != uploadProduce)
			{
				vsFileJob* fileJob = uploadJobs[uploadConsume];

				if (fileJob->data)
					delete[] fileJob->data;

				uploadConsume = (uploadConsume + 1) % fileJobMax;
				InterlockedDecrement((volatile long*)&jobsInFlight);
			}

			// Reset indirection table.
			i32 currentTexel = 0;
			for (i32 i = 0; i < virtualTexture.globalMipCount; ++i)
			{
				i32 mipSize = GetMipWidth(i, virtualTexture.globalMipCount);

				for (i32 t = 0; t < mipSize * mipSize; ++t)
				{
					virtualTexture.indirectionData[currentTexel++] = { 0, 0, 10 };
				}
			}

			updatedPageCache = true;
			input.purgeCache = false;
		}
		else
		{
			while (uploadConsume != uploadProduce && pagesToUploadMax)
			{
				--pagesToUploadMax;
				vsFileJob fileJob = *uploadJobs[uploadConsume];
				uploadConsume = (uploadConsume + 1) % fileJobMax;
				InterlockedDecrement((volatile long*)&jobsInFlight);

				vsCachePage* cachePage = GetCachePage(&vtCache, GetVirtualTexturePageHash(fileJob.pageX, fileJob.pageY, fileJob.pageMip));

				if (cachePage == NULL)
				{
					if (fileJob.data)
						delete[] fileJob.data;
				}
				else
				{
					// TODO: If the page is marked as rejected then ignore and kill page.

					updatedPageCache = true;
					vsCachePage* removedPage = NULL;

					if (vtCache.pageCount < vtCache.maxPageCount)
					{
						cachePage->cacheX = vtCache.pageCount % vtCache.width;
						cachePage->cacheY = vtCache.pageCount / vtCache.width;
						++vtCache.pageCount;

						if (vtCache.pagesLRUFirst == NULL)
						{
							vtCache.pagesLRUFirst = cachePage;
							vtCache.pagesLRULast = cachePage;
						}
						else
						{
							cachePage->nextLRUPage = vtCache.pagesLRUFirst;
							cachePage->prevLRUPage = NULL;
							vtCache.pagesLRUFirst->prevLRUPage = cachePage;
							vtCache.pagesLRUFirst = cachePage;
						}
					}
					else
					{
						removedPage = vtCache.pagesLRULast;
						vtCache.pagesLRULast = removedPage->prevLRUPage;
						removedPage->prevLRUPage->nextLRUPage = NULL;

						cachePage->nextLRUPage = vtCache.pagesLRUFirst;
						cachePage->prevLRUPage = NULL;
						vtCache.pagesLRUFirst->prevLRUPage = cachePage;
						vtCache.pagesLRUFirst = cachePage;

						cachePage->cacheX = removedPage->cacheX;
						cachePage->cacheY = removedPage->cacheY;

						RemoveCachePage(&vtCache, removedPage);
					}

					// Allocate new memory for upload page, prevents GPU stall while using old data.
					// TODO: But can this get out of hand?
					glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pageCachePBO);
					glBufferData(GL_PIXEL_UNPACK_BUFFER, 128 * 128 * 2, NULL, GL_STREAM_DRAW);
					uint32_t* pcuData = (uint32_t*)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);

					if (pcuData)
					{
						if (fileJob.data)
						{
							memcpy(pcuData, fileJob.data, 128 * 128 * 2);
							delete[] fileJob.data;
						}
						else
						{
							memcpy(pcuData, noPageFoundData, 128 * 128);
						}

						glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
					}

					//glBindBuffer(GL_PIXEL_UNPACK_BUFFER, transOutputSBO);

					glBindTexture(GL_TEXTURE_2D, pageCacheChannel0);
					// TODO: Check why this returns an error?
					glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, cachePage->cacheX * 128, cachePage->cacheY * 128, 128, 128, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 128 * 128, 0);

					glBindTexture(GL_TEXTURE_2D, pageCacheChannel1);
					glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, cachePage->cacheX * 128, cachePage->cacheY * 128, 128, 128, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 128 * 128, (void*)(128 * 128));

					glBindTexture(GL_TEXTURE_2D, 0);
					glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

					if (removedPage != NULL)
					{
						UpdateIndirectionTable(&virtualTexture, removedPage, false);
						delete[] removedPage;
					}

					UpdateIndirectionTable(&virtualTexture, cachePage, true);
				}
				//std::cout << "Process Job in " << (lz4Time * 1000.0) << "ms\n";
			}
		}

		/*
		vsCachePage* tempPage = vtCache.pagesLRUFirst;
		vsCachePage* tempPrevPage = NULL;
		i32 pageCount = 0;
		while (tempPage)
		{
			++pageCount;
			assert(tempPage->prevLRUPage == tempPrevPage);
			tempPrevPage = tempPage;
			tempPage = tempPage->nextLRUPage;
		}

		std::cout << pageCount << " Pages in chain\n";
		*/

		// Upload indirection changes.
		if (updatedPageCache)
		{
			//std::cout << "Uploaded " << 16 - pagesToUploadMax << "\n";
			// TODO: Segment the upload into smaller chunks?

			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, virtualTexture.indirectionPBO);
			vsIndirectionTableEntry* ipbo = (vsIndirectionTableEntry*)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);

			if (ipbo)
			{
				memcpy(ipbo, virtualTexture.indirectionData, virtualTexture.indirectionDataSizeBytes);
				glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
			}

			glBindTexture(GL_TEXTURE_2D, virtualTexture.indirectionTex);

			// Copy each mip level from the indirection data to the indirection texture.
			for (int i = 0; i < virtualTexture.globalMipCount; ++i)
			{
				i32 mipoffset = GetMipChainTexelOffset(i, virtualTexture.globalMipCount, virtualTexture.indirectionDataSizeBytes / sizeof(vsIndirectionTableEntry));
				i32 mipWidth = GetMipWidth(i, virtualTexture.globalMipCount);
				glTexSubImage2D(GL_TEXTURE_2D, i, 0, 0, mipWidth, mipWidth, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*)(mipoffset * sizeof(vsIndirectionTableEntry)));
			}

			glBindTexture(GL_TEXTURE_2D, 0);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}

		//-----------------------------------------------------------------------------------------------------------
		// Gather Feedback.
		//-----------------------------------------------------------------------------------------------------------
		bool doAnalyzeFeedback = (feedbackBuffer.writeIndex != feedbackBuffer.readIndex);

		feedbackBuffer.writeIndex = (feedbackBuffer.writeIndex + 1) % 2;
		feedbackBuffer.readIndex = (feedbackBuffer.writeIndex + 1) % 2;

		glBindBuffer(GL_PIXEL_PACK_BUFFER, feedbackBuffer.pixelBuffers[feedbackBuffer.writeIndex]);

		glBindTexture(GL_TEXTURE_2D, feedbackBuffer.imageBuffer);
		glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, 0);
		glBindTexture(GL_TEXTURE_2D, 0);

		glBindBuffer(GL_PIXEL_PACK_BUFFER, feedbackBuffer.pixelBuffers[feedbackBuffer.readIndex]);

		static u32* fbbCopy = new u32[160 * 120];
		u32* fbbData = (u32*)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);

		if (!fbbData)
		{
			std::cout << "Feedback Buffer failed to map.\n";
		}
		else
		{
			memcpy(fbbCopy, fbbData, sizeof(u32) * 160 * 120);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		}

		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

		//-----------------------------------------------------------------------------------------------------------
		// Analyze feedback buffer.
		//-----------------------------------------------------------------------------------------------------------
		if (doAnalyzeFeedback)
		{
			double fbbaTime = GetTime();

			int activePixels = 0;
			int uniquePixels = 0;
			int highestHashKeyIdx = 0;

			static i32* requiredPages = NULL;
			static i32* requiredPagesCounts = NULL;

			if (requiredPages == NULL)
			{
				requiredPages = new i32[virtualTexture.globalMipCount * 256];
				requiredPagesCounts = new i32[virtualTexture.globalMipCount];
			}

			memset(requiredPagesCounts, 0, sizeof(i32) * virtualTexture.globalMipCount);

			// Clear Hashmap
			for (int i = 0; i < feedbackBuffer.tileHashNodeCount; ++i)
			{
				feedbackBuffer.tileHashMap[i].keys[0] = -1;
			}

			// Gather unique pages from feedback buffer, sort based on mip.
			for (int i = 0; i < 160 * 120; ++i)
			{
				int x = fbbCopy[i] & 0xFFF;
				int y = (fbbCopy[i] >> 12) & 0xFFF;
				int mip = (fbbCopy[i] >> 24);

				if (mip < 11)
				{
					++activePixels;

					while (true)
					{
						// TODO: Why isn't the page hash the same as the feedback buffer data?
						int pageHash = GetVirtualTexturePageHash(x, y, mip);
						int hashMapIdx = pageHash % feedbackBuffer.tileHashNodeCount;

						int lastNode = 0;
						bool found = false;
						while (feedbackBuffer.tileHashMap[hashMapIdx].keys[lastNode] != -1)
						{
							if (feedbackBuffer.tileHashMap[hashMapIdx].keys[lastNode] == pageHash)
							{
								found = true;
								break;
							}

							++lastNode;
						}

						if (found)
						{
							break;
						}
						else
						{
							assert(lastNode < 16);
							if (lastNode > highestHashKeyIdx) highestHashKeyIdx = lastNode;
							feedbackBuffer.tileHashMap[hashMapIdx].keys[lastNode] = pageHash;
							// TODO: Don't set to -1 if we are at the end of a key bucket.
							feedbackBuffer.tileHashMap[hashMapIdx].keys[lastNode + 1] = -1;
							++uniquePixels;

							vsCachePage* page = GetCachePage(&vtCache, pageHash);
							if (page == NULL)
							{
								if (requiredPagesCounts[mip] < 256)
								{
									requiredPages[mip * 256 + requiredPagesCounts[mip]++] = pageHash;
								}
							}
							else
							{
								if (page->prevLRUPage != NULL)
								{
									// We know we are in the LRU and not at the first.

									page->prevLRUPage->nextLRUPage = page->nextLRUPage;

									if (page->nextLRUPage)
										page->nextLRUPage->prevLRUPage = page->prevLRUPage;
									else
										vtCache.pagesLRULast = page->prevLRUPage;

									page->nextLRUPage = vtCache.pagesLRUFirst;
									vtCache.pagesLRUFirst->prevLRUPage = page;
									vtCache.pagesLRUFirst = page;

									page->prevLRUPage = NULL;
								}
							}

							if (mip < virtualTexture.globalMipCount - 1)
							{
								x = x / 2;
								y = y / 2;
								++mip;
							}
						}
					}
				}
			}

			// TODO: To do page rejection we can iterate the inflight job loading and look into feedbackBuffer.tileHashMap.

			i32 pagesLoadMax = 32 - jobsInFlight;
			i32 loadingPages = 0;

			for (i32 i = virtualTexture.globalMipCount - 1; i >= 0; --i)
			{
				if (pagesLoadMax <= 0)
					break;

				for (i32 p = 0; p < requiredPagesCounts[i]; ++p)
				{
					if (pagesLoadMax-- > 0)
					{
						++loadingPages;
						i32 pageHash = requiredPages[i * 256 + p];

						int x = pageHash / 1000000;
						int y = (pageHash / 100) % 10000;
						int mip = pageHash % 100;

						LoadVirtualTexturePage(&vtCache, &virtualTexture, x, y, mip);
					}
				}
			}

			//std::cout << "Queing pages: " << loadingPages << " Jobs: " << jobsInFlight << "\n";

			fbbaTime = GetTime() - fbbaTime;
			//std::cout << "FFB: " << (fbbaTime * 1000.0) << "ms Active: " << uniquePixels << "/" << activePixels << " Highest Key: " << highestHashKeyIdx << " PIV: " << pagesInView << " PTL: " << pagesToLoad << "\n";
			//std::cout << "PIV: " << pagesInView << " PTL: " << pagesToLoad << "\n";

			/*
			// Examine feedback buffer at mouse position.
			vec2 mousePos = GetMousePosition() / vec2(gWidth, gHeight);
			if (mousePos.x >= 0.0f && mousePos.x <= 1.0f && mousePos.y >= 0.0f &&  mousePos.y <= 1.0f)
			{
			mousePos.y = 1.0f - mousePos.y;
			mousePos *= vec2(160, 120);
			i32 fbbX = (i32)mousePos.x;
			i32 fbbY = (i32)mousePos.y;

			u32 fbbData = fbbCopy[fbbY * 160 + fbbX];

			int x = fbbData & 0xFFF;
			int y = (fbbData >> 12) & 0xFFF;
			int mip = (fbbData >> 24);

			std::cout << x << " " << y << " " << mip << "\n";
			}
			*/
		}

		//-----------------------------------------------------------------------------------------------------------
		// Update Spherical Harmonics.
		//-----------------------------------------------------------------------------------------------------------
		glUseProgram(shSolveCompShader);
		glBindImageTexture(0, shBufferTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, envMap);
		glUniform4f(1, (float)GetTime(), (float)GetSinTime(1.0f), 0, 0);

		glDispatchCompute(1, 1, 1);
		glMemoryBarrier(GL_ALL_BARRIER_BITS);
		glBindTexture(GL_TEXTURE_2D, 0);

		//-----------------------------------------------------------------------------------------------------------
		// Main Pass.
		//-----------------------------------------------------------------------------------------------------------
		// Spherical Harmonics Test
		model = glm::translate(mat4(), vec3(0, 3, 3));
		model = glm::scale(model, vec3(0.01f, 0.01f, 0.01f));
		//model = glm::rotate(model, glm::radians((float)GetTime() * 90.0f), vec3Up);

		glUseProgram(shShaderProgram);
		glUniformMatrix4fv(0, 1, GL_FALSE, (float*)&proj);
		glUniformMatrix4fv(1, 1, GL_FALSE, (float*)&view);
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&model);
		glUniform4f(3, 1.0f, 1.0f, 0.0f, 0.0f);
		glUniform2f(4, (float)gWidth, (float)gHeight);
		glUniform3f(5, camera.camPos.x, camera.camPos.y, camera.camPos.z);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, envMap);

		glActiveTexture(GL_TEXTURE10);
		glBindTexture(GL_TEXTURE_2D, shBufferTex);

		glActiveTexture(GL_TEXTURE0);

		glUniform1i(0, 0);
		//glUniform1i(10, 10);

		glBindVertexArray(sphereVAO);
		glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_SHORT, 0);
		glBindVertexArray(0);

		// Light Balls
		glUseProgram(flatShaderProgram);
		glUniformMatrix4fv(0, 1, GL_FALSE, (float*)&proj);
		glUniformMatrix4fv(1, 1, GL_FALSE, (float*)&view);		
		glUniform4f(3, 1.0f, 1.0f, 0.0f, 0.0f);

		glBindVertexArray(sphereVAO);
		
		for (int i = 0; i < world.lightCount; ++i)
		{
			model = glm::translate(mat4(), world.lights[i].position);
			model = glm::scale(model, vec3(0.001f, 0.001f, 0.001f));
			glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&model);
			vec3 col = world.lights[i].color;
			glUniform4f(4, col.x, col.y, col.z, 0);
			glDrawElements(GL_TRIANGLES, sphereIndexCount, GL_UNSIGNED_SHORT, 0);
		}
		glBindVertexArray(0);

		// Virtual Forward Clustered.
		glUseProgram(svtForwardShaderProgram);
		glUniformMatrix4fv(0, 1, GL_FALSE, (float*)&proj);
		glUniformMatrix4fv(1, 1, GL_FALSE, (float*)&view);
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&model);
		glUniform4f(3, 1.0f, 1.0f, 0.0f, 0.0f);
		glUniform2f(4, (float)gWidth, (float)gHeight);
		glUniform3f(5, camera.camPos.x, camera.camPos.y, camera.camPos.z);

		glUniform1i(0, 0);
		glUniform1i(1, 1);		

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, pageCacheChannel0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, pageCacheChannel1);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, virtualTexture.indirectionTex);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, envMap);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);	
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_2D, irrMap);
		//glUniform1i(7, 7);

		//glActiveTexture(GL_TEXTURE0);

		glBindImageTexture(2, feedbackBuffer.imageBuffer, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
		
		glBindVertexArray(baronVAO);
		model = glm::translate(mat4(), vec3(0, 3, 3));
		model = glm::scale(model, vec3(0.1f, 0.1f, 0.1f));
		model = glm::rotate(model, glm::radians((float)GetTime() * 90.0f), vec3Up);
		
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&model);
		glDrawElements(GL_TRIANGLES, baronIndexCount, GL_UNSIGNED_SHORT, 0);

		glBindVertexArray(vao0);
		//glDrawElements(GL_TRIANGLES, indexCount0, GL_UNSIGNED_SHORT, 0);

		for (int i = 0; i < 10; ++i)
		{
			for (int j = 0; j < 10; ++j)
			{
				mat4 tempModelMat = glm::translate(mat4(), vec3(i * 6, 0, j * -20));
				glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&tempModelMat);
				glDrawElements(GL_TRIANGLES, indexCount0, GL_UNSIGNED_SHORT, 0);
			}
		}
		


		//*
		glBindVertexArray(planeVAO);
		mat4 planeMat = glm::scale(mat4(), vec3(10, 10, 10));
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&planeMat);		
		glUniform4f(3, 1.0f, 1.0f, 0.0f, 0.0f);
		glDrawElements(GL_TRIANGLES, planeIndexCount, GL_UNSIGNED_SHORT, 0);
		//*/

		glUseProgram(simpleVFCShaderProgram);
		glUniformMatrix4fv(0, 1, GL_FALSE, (float*)&proj);
		glUniformMatrix4fv(1, 1, GL_FALSE, (float*)&view);
		
		glUniform4f(3, 1.0f, 1.0f, 0.0f, 0.0f);
		glUniform2f(4, (float)gWidth, (float)gHeight);
		glUniform3f(5, camera.camPos.x, camera.camPos.y, camera.camPos.z);

		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D, scratchTexBC);
		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, scratchTexNM);
		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D, fuelTankCurveTex);
		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_2D, irrMap);
		glActiveTexture(GL_TEXTURE0);

		glUniform1i(4, 4);
		glUniform1i(5, 5);
		glUniform1i(6, 6);
		glUniform1i(7, 7);

		double texelSize = 1.0 / 131072.0;

		glBindVertexArray(vao1);
		
		glUniform3f(6, 1.0f, 0.3f, 0.0f);
		glUniform3f(7, 0.0f, 0.2f, 0.0f);

		mat4 fuelTankMat = glm::translate(mat4(), vec3(-4.0f, 0.0f, 0.0f));
		fuelTankMat = glm::scale(fuelTankMat, vec3(4, 4, 4));
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&fuelTankMat);
		//glUniform4f(3, 2048 * texelSize, 2048 * texelSize, 4096 * 17 * texelSize, 0.0f);
		glDrawElements(GL_TRIANGLES, indexCount1, GL_UNSIGNED_SHORT, 0);

		glUniform3f(6, 0.1f, 0.1f, 0.1f);
		glUniform3f(7, 0.0f, 0.85f, 0.0f);

		fuelTankMat = glm::translate(mat4(), vec3(-4.0f, 0.0f, -4.0f));
		fuelTankMat = glm::scale(fuelTankMat, vec3(4, 4, 4));
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&fuelTankMat);
		//glUniform4f(3, 2048 * texelSize, 2048 * texelSize, 4096 * 17 * texelSize, 0.0f);
		glDrawElements(GL_TRIANGLES, indexCount1, GL_UNSIGNED_SHORT, 0);

		glUniform3f(6, 0.8f, 0.8f, 0.8f);
		glUniform3f(7, 1.0f, 0.1f, 0.0f);

		fuelTankMat = glm::translate(mat4(), vec3(-4.0f, 0.0f, 4.0f));
		fuelTankMat = glm::scale(fuelTankMat, vec3(4, 4, 4));
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&fuelTankMat);
		//glUniform4f(3, 4096 * texelSize, 4096 * texelSize, 4096 * 16 * texelSize, 0.0f);
		glDrawElements(GL_TRIANGLES, indexCount1, GL_UNSIGNED_SHORT, 0);

		glUniform3f(6, 0.8f, 0.8f, 0.8f);
		glUniform3f(7, 1.0f, 0.5f, 0.0f);

		fuelTankMat = glm::translate(mat4(), vec3(0.0f, 0.0f, 4.0f));
		fuelTankMat = glm::scale(fuelTankMat, vec3(4, 4, 4));
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&fuelTankMat);
		//glUniform4f(3, 4096 * texelSize, 4096 * texelSize, 4096 * 16 * texelSize, 0.0f);
		glDrawElements(GL_TRIANGLES, indexCount1, GL_UNSIGNED_SHORT, 0);

		// Machine Block
		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D, machineBlockCurveTex);
		glActiveTexture(GL_TEXTURE0);

		glBindVertexArray(machineBlockVAO);
		glUniform3f(6, 250.0 / 255.0, 209.0 / 255.0, 201.0 / 255.0);
		glUniform3f(7, 1.0f, 0.3f, 0.0f);

		fuelTankMat = glm::translate(mat4(), vec3(-8.0f, -0.15f, 0.0f));
		fuelTankMat = glm::scale(fuelTankMat, vec3(1, 1, 1));
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&fuelTankMat);
		//glUniform4f(3, 4096 * texelSize, 4096 * texelSize, 4096 * 16 * texelSize, 0.0f);
		glDrawElements(GL_TRIANGLES, machineBlockIndexCount, GL_UNSIGNED_SHORT, 0);

		// Machine Block
		glBindVertexArray(machineBlockVAO);
		glUniform3f(6, 255.0 / 255.0, 229.0 / 255.0, 158 / 255.0);
		glUniform3f(7, 1.0f, 0.3f, 0.0f);

		fuelTankMat = glm::translate(mat4(), vec3(-8.0f, -0.15f, -3.0f));
		fuelTankMat = glm::scale(fuelTankMat, vec3(1, 1, 1));
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&fuelTankMat);
		//glUniform4f(3, 4096 * texelSize, 4096 * texelSize, 4096 * 16 * texelSize, 0.0f);
		glDrawElements(GL_TRIANGLES, machineBlockIndexCount, GL_UNSIGNED_SHORT, 0);

		// Machine Block
		glBindVertexArray(machineBlockVAO);
		//glUniform3f(6, 252.0 / 255.0, 250.0 / 255.0, 245.0 / 255.0);
		//glUniform3f(7, 1.0f, 0.3f, 0.0f);

		glUniform3f(6, 1.0f, 0.3f, 0.0f);
		//glUniform3f(6, 1.0f, 1.0f, 1.0f);
		//glUniform3f(7, 0.0f, 0.8f, 0.0f);
		glUniform3f(7, 0.0f, 0.3f, 0.0f);

		//fuelTankMat = glm::translate(mat4(), vec3(-8.0f, -0.15f, 3.0f));
		fuelTankMat = glm::translate(mat4(), vec3(-8.0f, 2.0f, 3.0f));
		fuelTankMat = glm::rotate(fuelTankMat, glm::radians((float)GetTime() * 45.0f), vec3(1.0f, 0.0f, 0.0f));
		fuelTankMat = glm::rotate(fuelTankMat, glm::radians((float)GetTime() * 45.0f), vec3(0.0f, 1.0f, 0.0f));
		fuelTankMat = glm::scale(fuelTankMat, vec3(1, 1, 1));
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&fuelTankMat);
		//glUniform4f(3, 4096 * texelSize, 4096 * texelSize, 4096 * 16 * texelSize, 0.0f);
		glDrawElements(GL_TRIANGLES, machineBlockIndexCount, GL_UNSIGNED_SHORT, 0);

		// Machine Block
		glBindVertexArray(machineBlockVAO);
		glUniform3f(6, 198.0 / 255.0, 198.0 / 255.0, 200.0 / 255.0);
		glUniform3f(7, 1.0f, 0.3f, 0.0f);

		fuelTankMat = glm::translate(mat4(), vec3(-8.0f, -0.15f, 6.0f));
		fuelTankMat = glm::scale(fuelTankMat, vec3(1, 1, 1));
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&fuelTankMat);
		//glUniform4f(3, 4096 * texelSize, 4096 * texelSize, 4096 * 16 * texelSize, 0.0f);
		glDrawElements(GL_TRIANGLES, machineBlockIndexCount, GL_UNSIGNED_SHORT, 0);

		// MGMK2
		/*
		glBindVertexArray(mgmk2VAO);
		glUniform3f(6, 0.8f, 0.8f, 0.8f);
		glUniform3f(7, 1.0f, 0.5f, 0.0f);
		
		fuelTankMat = glm::translate(mat4(), vec3(-8.0f, 0.6f, -5.0f));
		fuelTankMat = glm::rotate(fuelTankMat, glm::radians(180.0f), vec3(0.0f, 1.0f, 0.0f));
		fuelTankMat = glm::scale(fuelTankMat, vec3(0.5f, 0.5f, 0.5f));
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&fuelTankMat);
		//glUniform4f(3, 4096 * texelSize, 4096 * texelSize, 4096 * 16 * texelSize, 0.0f);
		glDrawElements(GL_TRIANGLES, mgmk2IndexCount, GL_UNSIGNED_SHORT, 0);
		*/
		
		// Sky sphere.
		glUseProgram(skySphereShaderProgram);
		mat4 skySphereMat = glm::translate(mat4(), camera.camPos);
		skySphereMat = glm::scale(skySphereMat, vec3(1000, 1000, 1000));
		
		glUniformMatrix4fv(0, 1, GL_FALSE, (float*)&proj);
		glUniformMatrix4fv(1, 1, GL_FALSE, (float*)&view);
		glUniformMatrix4fv(2, 1, GL_FALSE, (float*)&skySphereMat);
		glUniform4f(3, 1.0f, 1.0f, 0.0f, 0.0f);
		glUniform2f(4, (float)gWidth, (float)gHeight);
		glUniform3f(5, camera.camPos.x, camera.camPos.y, camera.camPos.z);

		//glUniform1i(0, 0);
		//glUniform1i(1, 1);

		glBindVertexArray(skySphereVAO);
		
		glDepthMask(GL_FALSE);
		glDrawElements(GL_TRIANGLES, skySphereIndexCount, GL_UNSIGNED_SHORT, 0);
		glDepthMask(GL_TRUE);

		glBindVertexArray(0);
		//*/

		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);



		/*
		// Wire frame overlay
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glEnable(GL_POLYGON_OFFSET_LINE);
		glDisable(GL_CULL_FACE);
		glDepthFunc(GL_LEQUAL);
		glUniform4f(3, 0.0f, 0.0f, 0.0f, 1);
		glPolygonOffset(-1.0f, 1.0f);
		//glDrawElements(GL_TRIANGLES, objModel.indexCount, GL_UNSIGNED_SHORT, 0);
		glBindVertexArray(0);
		glDisable(GL_POLYGON_OFFSET_LINE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		*/

		//-----------------------------------------------------------------------------------------------------------
		// SSAO Pass.
		//-----------------------------------------------------------------------------------------------------------

		// TODO: Investigate overall color being to dark?

		glBindFramebuffer(GL_FRAMEBUFFER, ssao.frameBuffer);
		glDisable(GL_DEPTH_TEST);
		// SRGB?
		glViewport(0, 0, gWidth / 2, gHeight / 2);

		glBindVertexArray(uiVAO);
		glUseProgram(ssao.shaderProgram);
		glUniform4f(0, 1.0f, 1.0f, -0.5f, -0.5f);
		glUniformMatrix4fv(1, 1, GL_FALSE, (float*)&invProj);
		glUniform4f(2, gWidth / 2, gHeight / 2, 0.0f, 0.0f);
		glUniformMatrix4fv(3, 1, GL_FALSE, (float*)&proj);
		glUniform3fv(4, 64, (GLfloat*)ssao.kernel);

		glUniform1i(0, 0);
		glUniform1i(2, 2);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, hdrFrameBufferDepthStencil);
		
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, gBufferNormalsColor);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, ssao.noiseTex);
		
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glBindVertexArray(0);
		
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);

		glBindFramebuffer(GL_FRAMEBUFFER, ssao.blurFrameBuffer);

		glBindVertexArray(uiVAO);

		glUseProgram(ssao.blurShaderProgram);
		glUniform4f(0, 1.0f, 1.0f, -0.5f, -0.5f);
		
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, ssao.colorBuffer);

		glDrawArrays(GL_TRIANGLES, 0, 6);

		glBindVertexArray(0);

		//-----------------------------------------------------------------------------------------------------------
		// Bloom Pass.
		//-----------------------------------------------------------------------------------------------------------
		glBindFramebuffer(GL_FRAMEBUFFER, bloom.tempFramebuffer);
		// SRGB?

		glViewport(0, 0, gWidth, gHeight);
		glUseProgram(ssao.compositeShaderProgram);
		glUniform4f(0, 1.0f, 1.0f, -0.5f, -0.5f);
		glBindVertexArray(uiVAO);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, hdrFrameBufferColor);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, ssao.blurColor);

		glDrawArrays(GL_TRIANGLES, 0, 6);

		glActiveTexture(GL_TEXTURE0);
		
		float spread = 1.0f;
		float blurSize = 2.0f;
		i32 blurIterations = 2;
		
		blurWidth = gWidth / 2;
		blurHeight = gHeight / 2;

		for (i32 i = 0; i < bloom.blurIterations; ++i)
		{
			glViewport(0, 0, blurWidth, blurHeight);
			glUseProgram(uiShaderProgram);
			glUniform4f(0, 1.0f, 1.0f, -0.5f, -0.5f);

			glBindFramebuffer(GL_FRAMEBUFFER, bloom.blurFramebuffers[i * 2]);

			if (i == 0)
				glBindTexture(GL_TEXTURE_2D, bloom.tempColor);
			else
				glBindTexture(GL_TEXTURE_2D, bloom.blurColors[(i - 1) * 2]);

			glDrawArrays(GL_TRIANGLES, 0, 6);

			glUseProgram(bloom.blurShaderProgram);
			glUniform4f(0, 1.0f, 1.0f, -0.5f, -0.5f);

			if (i > 0)
				spread = 1.0f;
			else
				spread = 0.5f;

			if (i == 1)
				spread = 0.75f;

			for (i32 j = 0; j < blurIterations; ++j)
			{
				float blur = (blurSize * 0.5f + j) * spread;

				glUniform2f(3, 0.0f, 1.0f / blurHeight * blur);
				glBindFramebuffer(GL_FRAMEBUFFER, bloom.blurFramebuffers[i * 2 + 1]);
				glBindTexture(GL_TEXTURE_2D, bloom.blurColors[i * 2 + 0]);
				glDrawArrays(GL_TRIANGLES, 0, 6);

				glUniform2f(3, 1.0f / blurWidth * blur, 0.0f);
				glBindFramebuffer(GL_FRAMEBUFFER, bloom.blurFramebuffers[i * 2 + 0]);
				glBindTexture(GL_TEXTURE_2D, bloom.blurColors[i * 2 + 1]);
				glDrawArrays(GL_TRIANGLES, 0, 6);
			}

			blurWidth /= 2;
			blurHeight /= 2;
		}

		//*
		glBindFramebuffer(GL_FRAMEBUFFER, hdrFramebuffer);
		glDepthFunc(GL_ALWAYS);
		glViewport(0, 0, gWidth, gHeight);

		glUseProgram(bloom.bloomShaderProgram);
		glUniform4f(0, 1.0f, 1.0f, -0.5f, -0.5f);
		glUniform1i(0, 0);
		glUniform1i(1, 1);
		glUniform1i(2, 2);
		glUniform1i(3, 3);
		glUniform1i(4, 4);
		glUniform1i(5, 5);
		glUniform1i(6, 6);
		glUniform1i(7, 7);
		//glUniform1i(8, 8);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, bloom.tempColor);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, bloom.blurColors[0]);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, bloom.blurColors[2]);

		glActiveTexture(GL_TEXTURE3);
		glBindTexture(GL_TEXTURE_2D, bloom.blurColors[4]);

		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D, bloom.blurColors[6]);

		glActiveTexture(GL_TEXTURE5);
		glBindTexture(GL_TEXTURE_2D, bloom.blurColors[8]);

		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D, bloom.blurColors[10]);

		glActiveTexture(GL_TEXTURE7);
		glBindTexture(GL_TEXTURE_2D, bloom.lensDirtTex);

		//glActiveTexture(GL_TEXTURE8);
		//glBindTexture(GL_TEXTURE_2D, ssao.blurColor);
		
		glDrawArrays(GL_TRIANGLES, 0, 6);

		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_DEPTH_TEST);
		//*/

		//-----------------------------------------------------------------------------------------------------------
		// HDR Tonemapping.
		//-----------------------------------------------------------------------------------------------------------
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, gWidth, gHeight);
		glEnable(GL_FRAMEBUFFER_SRGB);
		glDepthFunc(GL_ALWAYS);
		glDisable(GL_CULL_FACE);

		glUseProgram(tonemapShaderProgram);
		float exposure = 2.0f;
		glUniform4f(3, exposure, 0.0f, 0.0f, 0.0f);
		glUniform4f(0, 1.0f, 1.0f, -0.5f, -0.5f);

		glBindVertexArray(uiVAO);
		glBindTexture(GL_TEXTURE_2D, hdrFrameBufferColor);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);

		//-----------------------------------------------------------------------------------------------------------
		// UI Pass.
		//-----------------------------------------------------------------------------------------------------------				
		glDepthFunc(GL_ALWAYS);
		glDisable(GL_CULL_FACE);

		glUseProgram(uiShaderProgram);

		glBindVertexArray(uiVAO);
		glUniform4f(3, 1.0f, 1.0f, 1.0f, 1);
		
		//float uiCacheSize = 8192.0f * 2.0f;
		//glUniform4f(0, uiCacheSize / gWidth, uiCacheSize / gHeight, 0.5f - (uiCacheSize / gWidth), 0.0f - ((uiCacheSize) / gHeight));
		//glBindTexture(GL_TEXTURE_2D, pageCache);
		//glDrawArrays(GL_TRIANGLES, 0, 6);

		/*
		//glUniform4f(0, 256.0f / gWidth, 256.0f / gHeight, -0.5f, -0.5f);
		glUniform4f(0, 0.5f, 0.5f, -0.5f, -0.5f);
		glBindTexture(GL_TEXTURE_2D, ssao.blurColor);
		//glBindTexture(GL_TEXTURE_2D, gBufferNormalsColor);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		//*/

		glUniform4f(0, 256.0f / gWidth, 32.0f / gHeight, -0.5f, -0.5f);
		glBindTexture(GL_TEXTURE_2D, shBufferTex);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		
		float uiCacheSize = 128.0f;
		glUniform4f(0, uiCacheSize / gWidth, uiCacheSize / gHeight, 0.5f - (uiCacheSize / gWidth), -0.5f);
		glBindTexture(GL_TEXTURE_2D, pageCacheChannel0);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		glUniform4f(0, uiCacheSize / gWidth, uiCacheSize / gHeight, 0.5f - (uiCacheSize / gWidth) * 2.0f, -0.5f);
		glBindTexture(GL_TEXTURE_2D, virtualTexture.indirectionTex);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, (float)input.indirectionUIMipLevel);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, (float)input.indirectionUIMipLevel);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, -1000);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 1000);
		
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
		
		//-----------------------------------------------------------------------------------------------------------
		// Swap to screen.
		//-----------------------------------------------------------------------------------------------------------
		SwapBuffers(deviceContext);
	}

	//-----------------------------------------------------------------------------------------------------------
	// Destroy GL.
	//-----------------------------------------------------------------------------------------------------------
	// TODO: Destroy all the things!

	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(glContext);

	return 0;
}
