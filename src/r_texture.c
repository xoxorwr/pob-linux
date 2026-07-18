#include "r_texture.h"
#include <stdio.h>
#include <string.h>
#include <webp/decode.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

r_texManager_t g_texManager;

static const byte t_whiteImage[64] = {
	0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
	0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF
};

static const byte t_defaultTexture[64] = {
	0x7F,0x7F,0x7F,0x7F, 0x7F,0x7F,0x7F,0x7F,
	0x7F,0x7F,0x7F,0x00, 0x00,0x00,0x7F,0x7F,
	0x7F,0x7F,0x7F,0x00, 0x00,0x00,0x00,0x7F,
	0x7F,0x00,0x00,0x00, 0x00,0x00,0x00,0x7F,
	0x7F,0x00,0x00,0x00, 0x00,0x00,0x00,0x7F,
	0x7F,0x00,0x00,0x00, 0x00,0x00,0x00,0x7F,
	0x7F,0x00,0x00,0x00, 0x00,0x00,0x00,0x7F,
	0x7F,0x7F,0x7F,0x7F, 0x7F,0x7F,0x7F,0x7F
};

void r_texManagerInit(r_renderer_t *ren)
{
	g_texManager.ren = ren;
	g_texManager.uploadQueue.count = 0;
	pthread_mutex_init(&g_texManager.uploadQueue.mutex, NULL);
}

void r_texManagerShutdown(void)
{
	pthread_mutex_destroy(&g_texManager.uploadQueue.mutex);
}

void r_texManagerProcessUploads(void)
{
	pthread_mutex_lock(&g_texManager.uploadQueue.mutex);
	int count = g_texManager.uploadQueue.count;
	r_tex_t *queue[TEX_QUEUE_MAX];
	memcpy(queue, g_texManager.uploadQueue.queue, count * sizeof(r_tex_t *));
	g_texManager.uploadQueue.count = 0;
	pthread_mutex_unlock(&g_texManager.uploadQueue.mutex);

	for (int i = 0; i < count; i++) {
		r_tex_t *tex = queue[i];
		if (tex->texId) {
			glBindTexture(GL_TEXTURE_2D, tex->texId);
		}
	}
}

void r_texManagerEnqueue(r_tex_t *tex)
{
	pthread_mutex_lock(&g_texManager.uploadQueue.mutex);
	if (g_texManager.uploadQueue.count < TEX_QUEUE_MAX) {
		g_texManager.uploadQueue.queue[g_texManager.uploadQueue.count++] = tex;
	}
	pthread_mutex_unlock(&g_texManager.uploadQueue.mutex);
}

static void UploadTexture(r_tex_t *tex, const byte *pixels, int w, int h, int comp)
{
	glGenTextures(1, &tex->texId);
	glBindTexture(GL_TEXTURE_2D, tex->texId);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	GLenum internalFmt, fmt;
	if (comp == 4) {
		internalFmt = GL_RGBA8;
		fmt = GL_RGBA;
	} else if (comp == 3) {
		internalFmt = GL_RGB8;
		fmt = GL_RGB;
	} else {
		internalFmt = GL_R8;
		fmt = GL_RED;
	}

	glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, pixels);

	int filterMin = GL_LINEAR;
	int filterMag = GL_LINEAR;
	if (tex->flags & TF_NEAREST) {
		filterMag = GL_NEAREST;
	}
	if (!(tex->flags & TF_NOMIPMAP)) {
		if (comp == 1) {
			filterMin = GL_LINEAR;
		} else {
			filterMin = GL_LINEAR_MIPMAP_LINEAR;
			glGenerateMipmap(GL_TEXTURE_2D);
		}
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filterMin);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filterMag);

	if (tex->flags & TF_CLAMP) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	}

	tex->width = w;
	tex->height = h;
	tex->target = GL_TEXTURE_2D;
	tex->stackLayers = 1;

	glBindTexture(GL_TEXTURE_2D, 0);
}

static unsigned char* ReadWholeFile(const char* fileName, size_t* outSize) {
	FILE* f = fopen(fileName, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	if (size <= 0) {
		fclose(f);
		return NULL;
	}
	fseek(f, 0, SEEK_SET);
	unsigned char* buf = malloc(size);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t readBytes = fread(buf, 1, size, f);
	fclose(f);
	if (readBytes != (size_t)size) {
		free(buf);
		return NULL;
	}
	*outSize = (size_t)size;
	return buf;
}

static int LoadImageFile(r_tex_t *tex, const char *fileName)
{
	int w, h, comp;
	unsigned char *data = NULL;
	int isWebP = 0;

	const char *ext = strrchr(fileName, '.');
	if (ext && strcasecmp(ext, ".webp") == 0) {
		isWebP = 1;
		size_t fileSize = 0;
		unsigned char *fileData = ReadWholeFile(fileName, &fileSize);
		if (fileData) {
			data = WebPDecodeRGBA(fileData, fileSize, &w, &h);
			free(fileData);
			comp = 4;
			fprintf(stderr, "[LOAD] WebP: %s -> %s (w=%d, h=%d)\n", fileName, data ? "SUCCESS" : "DECODE_FAILED", w, h);
		} else {
			fprintf(stderr, "[LOAD] WebP: %s -> READ_FAILED\n", fileName);
		}
	} else {
		data = stbi_load(fileName, &w, &h, &comp, 0);
		if (!data) {
			fprintf(stderr, "[LOAD] STBI: %s -> FAILED\n", fileName);
		} else {
			fprintf(stderr, "[LOAD] STBI: %s -> SUCCESS (w=%d, h=%d)\n", fileName, w, h);
		}
	}

	if (!data) {
		tex->error = 1;
		return 1;
	}

	tex->fileWidth = w;
	tex->fileHeight = h;

	int maxDim = g_texManager.ren->texMaxDim;
	if (maxDim > 0 && (w > maxDim || h > maxDim)) {
		fprintf(stderr, "[LOAD] Texture dimension %dx%d exceeds maxDim %d\n", w, h, maxDim);
		if (isWebP) {
			WebPFree(data);
		} else {
			stbi_image_free(data);
		}
		tex->error = 1;
		return 1;
	}

	/* Normalize 1- and 2-channel images to RGBA for GLES compatibility */
	if (comp == 1 || comp == 2) {
		byte *rgba = (byte *)malloc((size_t)w * h * 4);
		if (rgba) {
			for (int i = 0; i < w * h; i++) {
				rgba[i * 4 + 0] = data[i * comp];
				rgba[i * 4 + 1] = data[i * comp];
				rgba[i * 4 + 2] = data[i * comp];
				rgba[i * 4 + 3] = (comp == 2) ? data[i * comp + 1] : 0xFF;
			}
			UploadTexture(tex, rgba, w, h, 4);
			free(rgba);
		} else {
			UploadTexture(tex, data, w, h, comp);
		}
	} else {
		UploadTexture(tex, data, w, h, comp);
	}
	if (isWebP) {
		WebPFree(data);
	} else {
		stbi_image_free(data);
	}
	tex->error = 0;
	return 0;
}

static void LoadDefaultTexture(r_tex_t *tex)
{
	static const byte default_rgba[4] = { 0xFF, 0x00, 0x00, 0xFF };
	tex->flags |= TF_NOMIPMAP;
	UploadTexture(tex, default_rgba, 1, 1, 4);
	tex->fileWidth = 1;
	tex->fileHeight = 1;
	tex->error = 0;
}

static void LoadWhiteTexture(r_tex_t *tex)
{
	static const byte white_rgba[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
	tex->flags |= TF_NOMIPMAP;
	UploadTexture(tex, white_rgba, 1, 1, 4);
	tex->fileWidth = 1;
	tex->fileHeight = 1;
	tex->error = 0;
}

r_tex_t *r_texCreate(r_renderer_t *ren, const char *fileName, int flags)
{
	r_tex_t *tex = (r_tex_t *)calloc(1, sizeof(r_tex_t));
	if (!tex) return NULL;

	tex->flags = flags;
	strncpy(tex->fileName, fileName, sizeof(tex->fileName) - 1);
	tex->fileName[sizeof(tex->fileName) - 1] = '\0';
	tex->format = 0;
	tex->stackLayers = 1;

	if (strcasecmp(fileName, "@white") == 0) {
		LoadWhiteTexture(tex);
		return tex;
	}

	if (LoadImageFile(tex, fileName)) {
		LoadDefaultTexture(tex);
	}

	return tex;
}

r_tex_t *r_texCreateFromData(r_renderer_t *ren, int width, int height, const byte *pixels, int flags)
{
	r_tex_t *tex = (r_tex_t *)calloc(1, sizeof(r_tex_t));
	if (!tex) return NULL;

	tex->flags = flags;
	tex->fileWidth = width;
	tex->fileHeight = height;
	strncpy(tex->fileName, "@data", sizeof(tex->fileName) - 1);

	UploadTexture(tex, pixels, width, height, 4);
	tex->error = 0;
	return tex;
}

void r_texDestroy(r_tex_t *tex)
{
	if (!tex) return;
	if (tex->texId) {
		glDeleteTextures(1, &tex->texId);
	}
	free(tex);
}

void r_texBind(r_tex_t *tex)
{
	if (tex && tex->texId) {
		glBindTexture(GL_TEXTURE_2D, tex->texId);
	} else {
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void r_texGetSize(r_tex_t *tex, int *width, int *height)
{
	if (tex) {
		if (width) *width = tex->fileWidth;
		if (height) *height = tex->fileHeight;
	} else {
		if (width) *width = 0;
		if (height) *height = 0;
	}
}
