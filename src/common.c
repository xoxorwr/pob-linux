#ifndef SG_COMMON_C
#define SG_COMMON_C

#include "common.h"

char* AllocString(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* out = (char*)malloc(len + 1);
    memcpy(out, str, len + 1);
    return out;
}

char* AllocStringLen(size_t len) {
    char* out = (char*)malloc(len + 1);
    out[0] = '\0';
    return out;
}

void FreeString(char* str) {
    free(str);
}

dword StringHash(const char* str, dword mod) {
    dword hash = 0;
    while (*str) {
        hash = (hash * 31 + (unsigned char)*str) % mod;
        str++;
    }
    return hash;
}

static const float colorEscape[10][3] = {
	{0.0f, 0.0f, 0.0f}, // ^Black
	{1.0f, 0.3f, 0.3f}, // ^Red
	{0.3f, 1.0f, 0.3f}, // ^Green
	{0.5f, 0.5f, 1.0f}, // ^Blue
	{1.0f, 1.0f, 0.3f}, // ^Yellow
	{1.0f, 0.3f, 1.0f}, // ^Purple
	{0.3f, 1.0f, 1.0f}, // ^Aqua
	{1.0f, 1.0f, 1.0f}, // ^White
	{0.6f, 0.6f, 0.6f}, // ^Gray
	{0.4f, 0.4f, 0.4f}  // ^DarkGray
};

int IsColorEscape(const char* str) {
	if (str[0] != '^' || str[1] == '\0') return 0;
	if (isdigit((unsigned char)str[1])) {
		return 2;
	}
	if (str[1] == 'x' || str[1] == 'X') {
		for (int c = 0; c < 6; c++) {
			if (!str[c + 2] || !isxdigit((unsigned char)str[c + 2])) {
				return 0;
			}
		}
		return 8;
	}
	return 0;
}

static int HexCharToInt(char c) {
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= '0' && c <= '9') return c - '0';
	return 0;
}

void ReadColorEscape(const char* str, int len, col4_t color) {
	if (len == 2) {
		int idx = str[1] - '0';
		if (idx >= 0 && idx <= 9) {
			color[0] = colorEscape[idx][0];
			color[1] = colorEscape[idx][1];
			color[2] = colorEscape[idx][2];
		}
		return;
	}
	if (len == 8) {
		int xr = (HexCharToInt(str[2]) << 4) | HexCharToInt(str[3]);
		int xg = (HexCharToInt(str[4]) << 4) | HexCharToInt(str[5]);
		int xb = (HexCharToInt(str[6]) << 4) | HexCharToInt(str[7]);
		color[0] = (float)xr / 255.0f;
		color[1] = (float)xg / 255.0f;
		color[2] = (float)xb / 255.0f;
	}
}

void Vec2Copy(const float* a, float* b) { b[0] = a[0]; b[1] = a[1]; }
void Vec4Copy(const float* a, float* b) { b[0] = a[0]; b[1] = a[1]; b[2] = a[2]; b[3] = a[3]; }

#endif
