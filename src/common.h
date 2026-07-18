#ifndef SG_COMMON_H
#define SG_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <strings.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <signal.h>

#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <time.h>
#include <sys/time.h>
#include <uuid/uuid.h>

typedef unsigned char byte;
typedef signed char sbyte;
typedef unsigned short word;
typedef unsigned int dword;

typedef float vec2_t[2];
typedef float vec3_t[3];
typedef float vec4_t[4];
typedef float col3_t[3];
typedef float col4_t[4];

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif

char* AllocString(const char* str);
char* AllocStringLen(size_t len);
void FreeString(char* str);
dword StringHash(const char* str, dword mod);

int IsColorEscape(const char* str);
void ReadColorEscape(const char* str, int len, col4_t color);

void Vec2Copy(const float* a, float* b);
void Vec4Copy(const float* a, float* b);

#endif
