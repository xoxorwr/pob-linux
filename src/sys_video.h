#ifndef SG_SYS_VIDEO_H
#define SG_SYS_VIDEO_H

#include "common.h"
#include "config.h"

typedef struct sys_main_s sys_main_t;

typedef void (*sys_keyEvent_f)(void *userData, int key, int type);
typedef void (*sys_charEvent_f)(void *userData, unsigned int codepoint);

typedef struct {
	int numMon;
	int priMon;
	struct {
		void *hnd;
		int left;
		int top;
		int width;
		int height;
	} mon[16];
} sys_monInfo_s;

typedef struct {
	int shown;
	int mode[2];
	int display;
	int fullscreen;
	int resizable;
} sys_vidSet_s;

typedef struct sys_video_s {
	int initialised;
	void *wnd;
	sys_vidSet_s cur;
	int scrSize[2];
	int minSize[2];
	char curTitle[512];
	int cursorInWindow;
	sys_monInfo_s monInfo;
	sys_main_t *sys;
	sys_keyEvent_f keyEvent;
	sys_charEvent_f charEvent;
	void *eventUserData;
	int lastClickKey;
	int lastClickX;
	int lastClickY;
} sys_video_t;

void sysVideoInit(sys_video_t *vid, sys_main_t *sys);
void sysVideoShutdown(sys_video_t *vid);
int  sysVideoApply(sys_video_t *vid, sys_vidSet_s *set);
void sysVideoSetForeground(sys_video_t *vid);
int  sysVideoIsActive(sys_video_t *vid);
void sysVideoSetVisible(sys_video_t *vid, int vis);
void sysVideoSetTitle(sys_video_t *vid, const char *title);
void *sysVideoGetWindowHandle(sys_video_t *vid);
void sysVideoGetRelativeCursor(sys_video_t *vid, int *x, int *y);
void sysVideoSetRelativeCursor(sys_video_t *vid, int x, int y);
int  sysVideoIsCursorOverWindow(sys_video_t *vid);
void sysVideoFramebufferSizeChanged(sys_video_t *vid, int width, int height);
void sysVideoSizeChanged(sys_video_t *vid, int width, int height, int maximized);
void sysVideoPosChanged(sys_video_t *vid, int x, int y);
void sysVideoGetMinSize(sys_video_t *vid, int *width, int *height);

#endif
