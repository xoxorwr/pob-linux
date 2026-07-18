#ifndef SG_SYS_MAIN_H
#define SG_SYS_MAIN_H

#include "common.h"
#include "config.h"
#include "keylist.h"

#include <sys/time.h>

typedef struct sys_video_s sys_video_t;
typedef struct sys_opengl_s sys_opengl_t;
typedef struct console_s console_t;

typedef struct core_main_s {
	void *data;
	void (*init)(struct core_main_s *core, int argc, char **argv);
	void (*frame)(struct core_main_s *core);
	void (*shutdown)(struct core_main_s *core);
	void (*keyEvent)(struct core_main_s *core, int key, int type);
} core_main_t;

typedef struct sys_main_s {
	char basePath[1024];
	char userPath[1024];
	console_t *con;
	sys_video_t *video;
	sys_opengl_t *gl;
	core_main_t *core;
	int x64;
	int debug;
	int processorCount;
	int initialised;
	volatile int exitFlag;
	volatile int restartFlag;
	char *exitMsg;
	char *threadError;
	int errorRaised;
	struct timeval baseTime;
	byte heldKeyState[KEY_SCROLL + 1];
	int minimized;
} sys_main_t;

extern sys_main_t g_sys;

void sysInit(int argc, char **argv);
void sysShutdown(void);
int  sysRun(int argc, char **argv);

int  sysGetTime(void);
void sysSleep(int msec);
int  sysIsKeyDown(byte key);
void sysClipboardCopy(const char *str);
char *sysClipboardPaste(void);
int  sysSetWorkDir(const char *path);
void sysSpawnProcess(const char *cmd, const char *args);
void sysOpenURLErr(const char *url);
void sysError(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void sysExit(const char *msg);
void sysRestart(void);

#endif
