#include "objLoader.h"

float _ParseInt(char* Buffer, int* Index, int Length)
{
	int part = 0;
	bool neg = false;

	int ret;

	while (*Index < Length && (Buffer[*Index] > '9' || Buffer[*Index] < '0') && Buffer[*Index] != '-')
		(*Index)++;

	// sign
	if (Buffer[*Index] == '-')
	{
		neg = true;
		(*Index)++;
	}

	// integer part
	while (*Index < Length && !(Buffer[*Index] > '9' || Buffer[*Index] < '0'))
		part = part * 10 + (Buffer[(*Index)++] - '0');

	ret = neg ? (part * -1) : part;
	return ret;
}

float _ParseFloat(char* Buffer, int* Index, int Length)
{
	int part = 0;
	bool neg = false;

	float ret;

	// find start
	while (*Index < Length && (Buffer[*Index] < '0' || Buffer[*Index] > '9') && Buffer[*Index] != '-' && Buffer[*Index] != '.')
		(*Index)++;

	// sign
	if (Buffer[*Index] == '-')
	{
		neg = true;
		(*Index)++;
	}

	// integer part
	while (*Index < Length && !(Buffer[*Index] > '9' || Buffer[*Index] < '0'))
		part = part * 10 + (Buffer[(*Index)++] - '0');

	ret = neg ? (float)(part * -1) : (float)part;

	// float part
	if (*Index < Length && Buffer[*Index] == '.')
	{
		(*Index)++;
		double mul = 1;
		part = 0;

		while (*Index < Length && !(Buffer[*Index] > '9' || Buffer[*Index] < '0'))
		{
			part = part * 10 + (Buffer[*Index] - '0');
			mul *= 10;
			(*Index)++;
		}

		if (neg)
			ret -= (float)part / (float)mul;
		else
			ret += (float)part / (float)mul;

	}

	// scientific part
	if (*Index < Length && (Buffer[*Index] == 'e' || Buffer[*Index] == 'E'))
	{
		(*Index)++;
		neg = (Buffer[*Index] == '-'); *Index++;
		part = 0;
		while (*Index < Length && !(Buffer[*Index] > '9' || Buffer[*Index] < '0'))
		{
			part = part * 10 + (Buffer[(*Index)++] - '0');
		}

		if (neg)
			ret /= (float)pow(10.0, (double)part);
		else
			ret *= (float)pow(10.0, (double)part);
	}

	return ret;
}

struct HashNode
{
	int			vertId;
	HashNode*	next;
};

HashNode** hashMap = new HashNode*[64007];
HashNode* hashNodes = new HashNode[64007];
int hashNodeCount = 0;

vsVertex*	verts = new vsVertex[64000];
vec3*		tempVerts = new vec3[64000];
vec2*		tempUVs = new vec2[64000];
vec3*		tempNormals = new vec3[64000];
uint16_t*	tris = new uint16_t[192000];

int			tempVertCount = 0;
int			tempUVCount = 0;
int			tempNormalCount = 0;
int			triCount = 0;
int			vertCount = 0;

int _GetUniqueVert(char* Buffer, int* Index, int Length)
{
	int vertId = _ParseInt(Buffer, Index, Length); (*Index)++;
	int uvId = _ParseInt(Buffer, Index, Length); (*Index)++;
	int normalId = _ParseInt(Buffer, Index, Length);

	uint32_t hash = 0;
	hash += ((int*)(tempVerts + (vertId - 1)))[0];
	hash += ((int*)(tempVerts + (vertId - 1)))[1];
	hash += ((int*)(tempVerts + (vertId - 1)))[2];

	hash += ((int*)(tempUVs + (uvId - 1)))[0];
	hash += ((int*)(tempUVs + (uvId - 1)))[1];

	hash += ((int*)(tempNormals + (normalId - 1)))[0];
	hash += ((int*)(tempNormals + (normalId - 1)))[1];
	hash += ((int*)(tempNormals + (normalId - 1)))[2];

	hash %= 64007;

	// See if hash exists
	int vertIndex = -1;
	HashNode* next = hashMap[hash];
	while (next != NULL)
	{
		if (verts[next->vertId].pos == tempVerts[vertId - 1] &&
			verts[next->vertId].uv == tempUVs[uvId - 1] &&
			verts[next->vertId].normal == tempNormals[normalId - 1])
		{
			vertIndex = next->vertId;
			break;
		}
		else
		{
			next = next->next;
		}
	}

	if (vertIndex == -1)
	{
		verts[vertCount].pos = tempVerts[vertId - 1];
		verts[vertCount].uv = tempUVs[uvId - 1];
		verts[vertCount].normal = tempNormals[normalId - 1];
		
		HashNode* hashNode = &hashNodes[hashNodeCount++];
		hashNode->next = hashMap[hash];
		hashNode->vertId = vertCount;
		hashMap[hash] = hashNode;

		return vertCount++;
	}
	
	return vertIndex;
}

vsOBJModel CreateOBJ(const char* FileName, vec4 UVScaleBias)
{
	vsOBJModel result = {};

	tempVertCount = 0;
	tempUVCount = 0;
	tempNormalCount = 0;
	triCount = 0;
	vertCount = 0;
	memset(hashMap, 0, sizeof(hashMap) * 64007);
	
	double time = GetTime();

	FILE* file = fopen(FileName, "rb");
	fseek(file, 0, SEEK_END);
	int fileLen = ftell(file);
	fseek(file, 0, SEEK_SET);
	char* fileBuffer = new char[fileLen + 1];
	fread(fileBuffer, 1, fileLen, file);
	fileBuffer[fileLen] = 0;

	int idx = 0;
	char c = fileBuffer[idx++];

	while (c != 0)
	{
		if (c == 'v')
		{
			c = fileBuffer[idx++];

			if (c == ' ')
			{
				vec3 attr;
				attr.x = _ParseFloat(fileBuffer, &idx, fileLen); idx++;
				attr.y = _ParseFloat(fileBuffer, &idx, fileLen); idx++;
				attr.z = _ParseFloat(fileBuffer, &idx, fileLen); idx++;
				c = fileBuffer[idx++];

				if (attr.x == 0.0f) attr.x = 0.0f;
				if (attr.y == 0.0f) attr.y = 0.0f;
				if (attr.z == 0.0f) attr.z = 0.0f;
				//attr.x = -attr.x;

				tempVerts[tempVertCount++] = attr;
			}
			else if (c == 't')
			{
				vec2 attr;
				attr.x = _ParseFloat(fileBuffer, &idx, fileLen); idx++;
				attr.y = _ParseFloat(fileBuffer, &idx, fileLen); idx++;
				c = fileBuffer[idx++];

				if (attr.x == 0.0f) attr.x = 0.0f;
				if (attr.y == 0.0f) attr.y = 0.0f;
				
				tempUVs[tempUVCount++] = attr;
			}
			else if (c == 'n')
			{
				vec3 attr;
				attr.x = _ParseFloat(fileBuffer, &idx, fileLen); idx++;
				attr.y = _ParseFloat(fileBuffer, &idx, fileLen); idx++;
				attr.z = _ParseFloat(fileBuffer, &idx, fileLen); idx++;
				c = fileBuffer[idx++];

				if (attr.x == 0.0f) attr.x = 0.0f;
				if (attr.y == 0.0f) attr.y = 0.0f;
				if (attr.z == 0.0f) attr.z = 0.0f;
				
				tempNormals[tempNormalCount++] = attr;
			}
		}
		else if (c == 'f')
		{
			c = fileBuffer[idx++];

			int rootVertId = _GetUniqueVert(fileBuffer, &idx, fileLen);
			int currVertId = _GetUniqueVert(fileBuffer, &idx, fileLen);
			c = fileBuffer[idx++];

			while (c == ' ')
			{
				int nextVertId = currVertId;
				currVertId = _GetUniqueVert(fileBuffer, &idx, fileLen);
				tris[triCount++] = rootVertId;
				tris[triCount++] = currVertId;
				tris[triCount++] = nextVertId;
				c = fileBuffer[idx++];
			}
		}
		else
		{
			while (c != '\n' && c != 0)
				c = fileBuffer[idx++];

			if (c == '\n')
				c = fileBuffer[idx++];
		}
	}

	delete[] fileBuffer;
	fclose(file);

	// TODO: The object just contains pointers to our permanent storage.
	result.verts = verts;
	result.indices = tris;
	result.vertCount = vertCount;
	result.indexCount = triCount;

	CalculateTangents(&result);

	for (int i = 0; i < vertCount; ++i)
	{
		verts[i].uv.y = 1.0f - verts[i].uv.y;
		verts[i].uv *= vec2(UVScaleBias.x, UVScaleBias.y) / 131072.0f;
		verts[i].uv += vec2(UVScaleBias.z, UVScaleBias.w) / 131072.0f;
	}
	
	time = GetTime() - time;
	
	std::cout << "Loaded OBJ " << FileName << " in " << (time * 1000.0) << "ms Verts: " << vertCount << " Tris: " << (triCount / 3) << "\n";

	/*
	// Determine how good the hash table usage is.
	int maxCollisions = 0;
	int emptyBuckets = 0;

	int entryCount[5] = {};

	for (int i = 0; i < 64007; ++i)
	{
		if (hashMap[i] == NULL)
		{
			++emptyBuckets;
			entryCount[0]++;
		}
		else
		{
			int entries = 0;
			HashNode *next = hashMap[i];
			while (next != NULL)
			{
				++entries;
				next = next->next;
			}

			if (entries > maxCollisions)
				maxCollisions = entries;

			entryCount[entries]++;
		}
	}

	std::cout << "Hash stats - Empty Buckets: " << emptyBuckets << " Max Entires: " << maxCollisions << "\n";

	for (int i = 0; i < 5; ++i)
	{
		std::cout << "Entries[" << i << "]: " << entryCount[i] << "\n";
	}
	//*/

	return result;
}

void CalculateTangents(vsOBJModel* Model)
{
	double time = GetTime();

	static vec3 tan1[64000];
	static vec3 tan2[64000];

	memset(tan1, 0, sizeof(tan1));
	memset(tan2, 0, sizeof(tan2));

	for (i32 i = 0; i < Model->indexCount; i += 3)
	{
		i32 i1 = Model->indices[i + 0];
		i32 i2 = Model->indices[i + 1];
		i32 i3 = Model->indices[i + 2];

		vec3 v1 = Model->verts[i1].pos;
		vec3 v2 = Model->verts[i2].pos;
		vec3 v3 = Model->verts[i3].pos;

		vec2 w1 = Model->verts[i1].uv;
		vec2 w2 = Model->verts[i2].uv;
		vec2 w3 = Model->verts[i3].uv;

		f32 x1 = v2.x - v1.x;
		f32 x2 = v3.x - v1.x;
		f32 y1 = v2.y - v1.y;
		f32 y2 = v3.y - v1.y;
		f32 z1 = v2.z - v1.z;
		f32 z2 = v3.z - v1.z;

		f32 s1 = w2.x - w1.x;
		f32 s2 = w3.x - w1.x;
		f32 t1 = w2.y - w1.y;
		f32 t2 = w3.y - w1.y;

		f32 d = (s1 * t2 - s2 * t1);
		f32 r = 0.0f;

		if (d != 0)
			r = 1.0f / d;
		
		vec3 sdir = vec3((t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r);
		vec3 tdir = vec3((s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r);

		tan1[i1] += sdir;
		tan1[i2] += sdir;
		tan1[i3] += sdir;

		tan2[i1] += tdir;
		tan2[i2] += tdir;
		tan2[i3] += tdir;
	}


	for (i32 i = 0; i < Model->vertCount; ++i)
	{
		vec3 n = Model->verts[i].normal;
		vec3 t = tan1[i];

		// GS Orthonormalize
		n = glm::normalize(n);
		t = glm::normalize(t);
		vec3 proj = n * glm::dot(t, n);
		t = t - proj;
		t = glm::normalize(t);

		vec4 tangent;
		tangent.x = t.x;
		tangent.y = t.y;
		tangent.z = t.z;
		tangent.w = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
		
		Model->verts[i].tangent = tangent;
	}

	time = GetTime() - time;
	//std::cout << "Tangent time: " << (time * 1000.0) << "ms\n";
}