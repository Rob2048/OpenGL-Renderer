#include "shaderCompile.h"

enum vsTokenType
{
	TT_PREPROCESSOR,
	TT_INCLUDE,
	TT_STRING,
	TT_EOF,
	TT_UNKNOWN,
};

struct vsToken
{
	vsTokenType	type;
	char*		lexeme;
	i32			length;
};

struct vsTokenizer
{
	char*	at;
	vsToken	nextToken;
};

struct vsShaderParser
{
	char* includedFiles[256];
	i32 includedFileCount;
};

char* ReadFileWithNull(char* FileName, i32* DataSize)
{
	FILE *file = fopen(FileName, "rb");

	if (file == NULL)
	{
		std::cout << "File not found: " << FileName << "\n";
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	int fileLen = ftell(file);
	fseek(file, 0, SEEK_SET);
	char* data = new char[fileLen + 1];
	fread(data, 1, fileLen, file);
	data[fileLen] = 0;
	fclose(file);

	*DataSize = fileLen;

	return data;
}

__forceinline bool IsWhitespace(char C)
{
	return (C == ' ' || C == '\t' || C == '\r');
}

__forceinline bool IsAlpha(char C)
{
	return (C >= 'A' && C <= 'Z') || (C >= 'a' && C <= 'z');
}

__forceinline bool IsDigit(char C)
{
	return (C >= '0' && C <= '9');
}

__forceinline bool IsNum(char C)
{
	return IsDigit(C) || (C == '-' || C == '.');
}

__forceinline bool IsAlphaNum(char C)
{
	return IsAlpha(C) || IsNum(C);
}

bool ClearWhitespace(vsTokenizer* Tokenizer)
{
	bool newLine = false;

	while (true)
	{
		while (IsWhitespace(Tokenizer->at[0]))
		{
			++Tokenizer->at;
		}

		if (Tokenizer->at[0] == '\n')
		{
			newLine = true;
			++Tokenizer->at;
		}
		else
		{
			break;
		}
	}

	return newLine;
}

void ClearComments(vsTokenizer* Tokenizer)
{
	if (Tokenizer->at[0] == '/')
	{
		++Tokenizer->at;

		if (Tokenizer->at[0] == '/')
		{
			++Tokenizer->at;

			while (Tokenizer->at[0] != '\n' && Tokenizer->at[0] != 0)
			{
				++Tokenizer->at;
			}
		}
		else if (Tokenizer->at[0] == '*')
		{
			++Tokenizer->at;

			while (true)
			{
				if (Tokenizer->at[0] == '*')
				{
					++Tokenizer->at;

					if (Tokenizer->at[0] == '/')
					{
						++Tokenizer->at;
						break;
					}
				}
				else if (Tokenizer->at[0] == 0)
				{
					break;
				}
				else
				{
					++Tokenizer->at;
				}
			}
		}
	}
}

bool CompareLexeme(char* String, char* Lexeme, i32 LexemeLength)
{
	i32 strLen = (i32)strlen(String);
	
	if (strLen != LexemeLength)
		return false;

	if (memcmp(String, Lexeme, strLen) == 0)
		return true;

	return false;
}

void AdvanceToken(vsTokenizer* Tokenizer)
{
	vsToken token = {};

	bool newLine = ClearWhitespace(Tokenizer);
	ClearComments(Tokenizer);

	if (newLine && Tokenizer->at[0] == '#')
	{
		++Tokenizer->at;
		token.type = TT_PREPROCESSOR;
		token.lexeme = Tokenizer->at;
		
		while (IsAlpha(Tokenizer->at[0]))
		{
			++Tokenizer->at;
			++token.length;
		}

		if (CompareLexeme("include", token.lexeme, token.length))
		{
			token.type = TT_INCLUDE;			
		}
	}
	else if (Tokenizer->at[0] == '"')
	{
		++Tokenizer->at;
		token.type = TT_STRING;
		token.lexeme = Tokenizer->at;

		while (Tokenizer->at[0] != '"')
		{
			++Tokenizer->at;
			++token.length;
		}

		++Tokenizer->at;
	}
	else if (Tokenizer->at[0] == 0)
	{
		token.type = TT_EOF;
		++Tokenizer->at;
	}
	else
	{
		token.type = TT_UNKNOWN;
		++Tokenizer->at;
	}

	Tokenizer->nextToken = token;
}

i32 StripPath(char* FileName, char* Path)
{
	i32 len = (i32)strlen(FileName);
	i32 filenameLen = 0;
	char* c = FileName + len - 1;

	while (c > FileName && *c != '\\' && *c != '/')
	{
		++filenameLen;
		--c;
	}

	i32 pathLen = len - filenameLen;
	memcpy(Path, FileName, pathLen);
	Path[pathLen] = 0;

	return pathLen;
}

i32 ReplaceText(char** Original, i32 OriginalLen, i32 ReplaceAt, i32 ReplaceCount, char* SrcText, i32 SrcTextLen)
{	
	i32 newTotalSize = OriginalLen - ReplaceCount + SrcTextLen;
	


	return newTotalSize;
}

i32 AppendText(char** Dst, i32 DstLen, char* Src, i32 SrcLen)
{
	i32 newLen = DstLen + SrcLen;

	char* temp = new char[newLen];

	memcpy(temp, *Dst, DstLen);
	memcpy(temp + DstLen, Src, SrcLen);

	if (*Dst)
		delete[] *Dst;

	*Dst = temp;

	return newLen;
}

char* scompCompileShader(char* FileName, i32* DataSize, vsManagedDependency** DepList)
{
	if (DepList != NULL)
	{
		vsManagedDependency* dep = new vsManagedDependency();
		dep->lastUpdated = 0;
		strcpy(dep->Name, FileName);
		dep->next = NULL;

		vsManagedDependency* findDep = *DepList;

		bool found = false;

		if (findDep != NULL)
		{
			while (true)
			{
				if (strcmp(findDep->Name, FileName) == 0)
				{
					found = true;
					break;
				}
				
				if (findDep->next == NULL)
				{
					break;
				}

				findDep = findDep->next;
			}

			if (!found)
			{
				findDep->next = dep;
			}
		}
		else
		{
			*DepList = dep;
		}
	}

	//std::cout << "GLSL Parser " << FileName << "\n";

	char path[256];
	i32 pathLen = StripPath(FileName, path);

	char* shaderData = ReadFileWithNull(FileName, DataSize);
	char* compositeData = NULL;
	i32 compositeDataLen = 0;
	char* shaderDataCompositePtr = shaderData;

	if (shaderData == NULL)
	{
		std::cout << "Can't read file: " << FileName << "\n";
		return NULL;
	}

	vsTokenizer tokenizer = {};
	tokenizer.at = shaderData;

	AdvanceToken(&tokenizer);

	bool parsing = true;
	while (parsing)
	{
		switch (tokenizer.nextToken.type)
		{
			case TT_EOF:
			{
				parsing = false;
			} break;

			case TT_INCLUDE:
			{
				char* replaceStart = tokenizer.nextToken.lexeme - 1;
				AdvanceToken(&tokenizer);

				if (tokenizer.nextToken.type == TT_STRING)
				{
					char includeFile[256] = {};
					memcpy(includeFile, path, pathLen);
					memcpy(includeFile + pathLen, tokenizer.nextToken.lexeme, tokenizer.nextToken.length);
					//std::cout << "Include file: " << includeFile << "\n";

					i32 includeSize = 0;
					char* includeData = scompCompileShader(includeFile, &includeSize, DepList);

					compositeDataLen = AppendText(&compositeData, compositeDataLen, shaderDataCompositePtr, (i32)(replaceStart - shaderDataCompositePtr));
					compositeDataLen = AppendText(&compositeData, compositeDataLen, includeData, includeSize);

					shaderDataCompositePtr = tokenizer.at;

					if (includeData == NULL)
					{
						std::cout << "Can't load include file: " << includeFile << "\n";
						return NULL;
					}

					AdvanceToken(&tokenizer);
				}

			} break;

			case TT_UNKNOWN:
			{
				AdvanceToken(&tokenizer);
			} break;

			default:
			{
				/*
				std::cout << "Token " << tokenizer.nextToken.type << " [";
				std::cout.write(tokenizer.nextToken.lexeme, tokenizer.nextToken.length);
				std::cout << "]\n";
				*/

				AdvanceToken(&tokenizer);
			} break;
		}
	}

	compositeDataLen = AppendText(&compositeData, compositeDataLen, shaderDataCompositePtr, (i32)(tokenizer.at - shaderDataCompositePtr));
	*DataSize = compositeDataLen - 1;

	delete[] shaderData;

	return compositeData;
}