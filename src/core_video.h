#ifndef SG_CORE_VIDEO_H
#define SG_CORE_VIDEO_H

#include "common.h"

typedef struct sys_video_s sys_video_t;
typedef struct coreConfig_s coreConfig_t;

typedef struct coreVideo_s {
	int wndX;
	int wndY;
	int wndW;
	int wndH;
	int maximized;
	int fullscreen;
	int display;
	int mode[2];
	int resizable;
} coreVideo_t;

void coreVideoInit(coreVideo_t *vid);
void coreVideoShutdown(coreVideo_t *vid);
void coreVideoApply(coreVideo_t *vid, sys_video_t *sysVid, coreConfig_t *cfg);
void coreVideoSave(coreVideo_t *vid, sys_video_t *sysVid, coreConfig_t *cfg);

#endif
