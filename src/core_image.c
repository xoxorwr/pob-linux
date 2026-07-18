#include "../dep/stb_image.h"

#include "core_image.h"

int coreImageLoad(coreImage_t *img, const char *path, int desiredChannels)
{
	memset(img, 0, sizeof(*img));
	img->data = stbi_load(path, &img->width, &img->height, &img->channels, desiredChannels);
	if (!img->data)
		return 0;
	return 1;
}

void coreImageFree(coreImage_t *img)
{
	if (img->data) {
		stbi_image_free(img->data);
		img->data = NULL;
	}
	img->width = 0;
	img->height = 0;
	img->channels = 0;
}

int coreImageGetSize(const char *path, int *width, int *height)
{
	int w, h, c;
	if (!stbi_info(path, &w, &h, &c))
		return 0;
	if (width)  *width  = w;
	if (height) *height = h;
	return 1;
}
