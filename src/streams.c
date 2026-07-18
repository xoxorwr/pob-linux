#include "streams.h"

#define MEM_GROW 4096

/* ------------------------------------------------------------------ */
/* MemoryStream                                                        */
/* ------------------------------------------------------------------ */

MemoryStream* memStreamCreate(size_t initialCapacity) {
	MemoryStream* s = (MemoryStream*)calloc(1, sizeof(MemoryStream));
	if (!s)
		return NULL;

	if (initialCapacity < 256)
		initialCapacity = 256;

	s->data = (unsigned char*)malloc(initialCapacity);
	if (!s->data) {
		free(s);
		return NULL;
	}

	s->capacity = initialCapacity;
	s->size     = 0;
	s->pos      = 0;
	s->ownsData = 1;
	return s;
}

void memStreamDestroy(MemoryStream* s) {
	if (!s)
		return;
	if (s->ownsData && s->data)
		free(s->data);
	free(s);
}

static int memStreamGrow(MemoryStream* s, size_t needed) {
	size_t newCap = s->capacity;
	while (newCap < needed)
		newCap += MEM_GROW;

	unsigned char* tmp = (unsigned char*)realloc(s->data, newCap);
	if (!tmp)
		return 0;

	s->data     = tmp;
	s->capacity = newCap;
	return 1;
}

size_t memStreamRead(MemoryStream* s, void* buf, size_t len) {
	if (!s || !buf)
		return 0;

	size_t avail = s->size - s->pos;
	if (len > avail)
		len = avail;

	memcpy(buf, s->data + s->pos, len);
	s->pos += len;
	return len;
}

size_t memStreamWrite(MemoryStream* s, const void* buf, size_t len) {
	if (!s || !buf || len == 0)
		return 0;

	size_t end = s->pos + len;
	if (end > s->capacity) {
		if (!memStreamGrow(s, end))
			return 0;
	}

	memcpy(s->data + s->pos, buf, len);
	s->pos = end;
	if (s->pos > s->size)
		s->size = s->pos;
	return len;
}

int memStreamSeek(MemoryStream* s, long offset, int whence) {
	if (!s)
		return -1;

	long newPos;
	switch (whence) {
	case SEEK_SET: newPos = offset; break;
	case SEEK_CUR: newPos = (long)s->pos + offset; break;
	case SEEK_END: newPos = (long)s->size + offset; break;
	default: return -1;
	}

	if (newPos < 0)
		newPos = 0;
	if ((size_t)newPos > s->size)
		newPos = (long)s->size;

	s->pos = (size_t)newPos;
	return 0;
}

long memStreamTell(MemoryStream* s) {
	if (!s)
		return -1;
	return (long)s->pos;
}

unsigned char* memStreamData(MemoryStream* s) {
	if (!s)
		return NULL;
	return s->data;
}

size_t memStreamSize(MemoryStream* s) {
	if (!s)
		return 0;
	return s->size;
}

/* ------------------------------------------------------------------ */
/* FileStream                                                           */
/* ------------------------------------------------------------------ */

FileStream* fileStreamOpen(const char* path, const char* mode) {
	if (!path || !mode)
		return NULL;

	FILE* fp = fopen(path, mode);
	if (!fp)
		return NULL;

	FileStream* s = (FileStream*)calloc(1, sizeof(FileStream));
	if (!s) {
		fclose(fp);
		return NULL;
	}

	s->handle    = fp;
	s->ownsHandle = 1;
	return s;
}

void fileStreamClose(FileStream* s) {
	if (!s)
		return;
	if (s->ownsHandle && s->handle)
		fclose(s->handle);
	free(s);
}

size_t fileStreamRead(FileStream* s, void* buf, size_t len) {
	if (!s || !s->handle || !buf)
		return 0;
	return fread(buf, 1, len, s->handle);
}

size_t fileStreamWrite(FileStream* s, const void* buf, size_t len) {
	if (!s || !s->handle || !buf)
		return 0;
	return fwrite(buf, 1, len, s->handle);
}

int fileStreamSeek(FileStream* s, long offset, int whence) {
	if (!s || !s->handle)
		return -1;
	return fseek(s->handle, offset, whence);
}

long fileStreamTell(FileStream* s) {
	if (!s || !s->handle)
		return -1;
	return ftell(s->handle);
}

long fileStreamSize(FileStream* s) {
	if (!s || !s->handle)
		return -1;

	long cur = ftell(s->handle);
	fseek(s->handle, 0, SEEK_END);
	long sz = ftell(s->handle);
	fseek(s->handle, cur, SEEK_SET);
	return sz;
}

int fileStreamEOF(FileStream* s) {
	if (!s || !s->handle)
		return 1;
	return feof(s->handle);
}
