#ifndef SG_CORE_IMAGE_H
#define SG_CORE_IMAGE_H

#include "common.h"

typedef struct coreImage_s {
	unsigned char *data;
	int width;
	int height;
	int channels;
} coreImage_t;

int  coreImageLoad(coreImage_t *img, const char *path, int desiredChannels);
void coreImageFree(coreImage_t *img);
int  coreImageGetSize(const char *path, int *width, int *height);

#endif
