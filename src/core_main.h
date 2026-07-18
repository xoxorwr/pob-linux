#ifndef SG_CORE_MAIN_H
#define SG_CORE_MAIN_H

#include "common.h"
#include "core_config.h"
#include "core_video.h"

typedef struct sys_main_s sys_main_t;

typedef struct core_s {
	sys_main_t  *sys;
	coreConfig_t config;
	coreVideo_t  video;
	void        *uiData;
	void (*uiFrame)(void *uiData);
} core_t;

void coreInit(core_t *core, sys_main_t *sys, int argc, char **argv);
void coreFrame(core_t *core);
void coreShutdown(core_t *core);

#endif
