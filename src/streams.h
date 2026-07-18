#ifndef SG_STREAMS_H
#define SG_STREAMS_H

#include "common.h"

typedef struct {
	unsigned char* data;
	size_t         size;
	size_t         pos;
	size_t         capacity;
	int            ownsData;
} MemoryStream;

typedef struct {
	FILE*  handle;
	int    ownsHandle;
} FileStream;

MemoryStream* memStreamCreate(size_t initialCapacity);
void          memStreamDestroy(MemoryStream* s);
size_t        memStreamRead(MemoryStream* s, void* buf, size_t len);
size_t        memStreamWrite(MemoryStream* s, const void* buf, size_t len);
int           memStreamSeek(MemoryStream* s, long offset, int whence);
long          memStreamTell(MemoryStream* s);
unsigned char* memStreamData(MemoryStream* s);
size_t        memStreamSize(MemoryStream* s);

FileStream* fileStreamOpen(const char* path, const char* mode);
void        fileStreamClose(FileStream* s);
size_t      fileStreamRead(FileStream* s, void* buf, size_t len);
size_t      fileStreamWrite(FileStream* s, const void* buf, size_t len);
int         fileStreamSeek(FileStream* s, long offset, int whence);
long        fileStreamTell(FileStream* s);
long        fileStreamSize(FileStream* s);
int         fileStreamEOF(FileStream* s);

#endif
