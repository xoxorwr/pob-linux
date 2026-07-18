#ifndef SG_SYS_OPENGL_H
#define SG_SYS_OPENGL_H

#include "common.h"

typedef struct sys_main_s sys_main_t;

typedef struct {
	int bColor;
	int bDepth;
	int bStencil;
	int vsync;
} sys_glSet_s;

typedef struct sys_opengl_s {
	int initialised;
	sys_main_t *sys;
} sys_opengl_t;

void sysGLInit(sys_opengl_t *gl, sys_main_t *sys, sys_glSet_s *set);
void sysGLShutdown(sys_opengl_t *gl);
void sysGLSwap(sys_opengl_t *gl);
void *sysGLGetProc(const char *name);

#endif
