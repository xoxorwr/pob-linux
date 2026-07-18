#ifndef SG_R_TEXTURE_H
#define SG_R_TEXTURE_H

#include "r_main.h"

/* Texture status */
typedef enum {
	TEX_INIT = 0,
	TEX_DONE
} r_texStatus_e;

/* Async upload queue */
#define TEX_QUEUE_MAX 4096

typedef struct {
	r_tex_t *queue[TEX_QUEUE_MAX];
	int count;
	pthread_mutex_t mutex;
} r_texQueue_t;

/* Texture manager */
typedef struct {
	r_renderer_t *ren;
	r_texQueue_t uploadQueue;
} r_texManager_t;

extern r_texManager_t g_texManager;

void r_texManagerInit(r_renderer_t *ren);
void r_texManagerShutdown(void);
void r_texManagerProcessUploads(void);
void r_texManagerEnqueue(r_tex_t *tex);

#endif
