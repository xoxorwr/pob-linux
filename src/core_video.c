#include "core_video.h"
#include "core_config.h"
#include "sys_video.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

void coreVideoInit(coreVideo_t *vid)
{
	memset(vid, 0, sizeof(*vid));
	vid->wndW = 1280;
	vid->wndH = 720;
	vid->mode[0] = -1;
	vid->mode[1] = -1;
	vid->display = -1;
	vid->fullscreen = 0;
	vid->resizable = 0;
	vid->maximized = 0;
}

void coreVideoShutdown(coreVideo_t *vid)
{
	(void)vid;
}

void coreVideoApply(coreVideo_t *vid, sys_video_t *sysVid, coreConfig_t *cfg)
{
	vid->mode[0] = coreConfigGetInt(cfg, "vid_mode_x", atoi(CFG_VID_DEFMODE));
	vid->mode[1] = coreConfigGetInt(cfg, "vid_mode_y", atoi(CFG_VID_DEFMODE));
	vid->display = coreConfigGetInt(cfg, "vid_display", atoi(CFG_VID_DEFDISPLAY));
	vid->fullscreen = coreConfigGetBool(cfg, "vid_fullscreen", atoi(CFG_VID_DEFFULLSCREEN));
	vid->resizable = coreConfigGetBool(cfg, "vid_resizable", atoi(CFG_VID_DEFRESIZABLE));
	vid->wndX = coreConfigGetInt(cfg, "vid_wnd_x", -1);
	vid->wndY = coreConfigGetInt(cfg, "vid_wnd_y", -1);
	vid->wndW = coreConfigGetInt(cfg, "vid_wnd_w", 1280);
	vid->wndH = coreConfigGetInt(cfg, "vid_wnd_h", 720);
	vid->maximized = coreConfigGetBool(cfg, "vid_maximized", 0);

	int display = vid->display;
	if (display < 0) display = 0;
	if (display >= sysVid->monInfo.numMon)
		display = 0;

	sys_vidSet_s set;
	memset(&set, 0, sizeof(set));
	set.shown = 1;
	set.mode[0] = vid->wndW;
	set.mode[1] = vid->wndH;
	set.display = display;
	set.fullscreen = vid->fullscreen;
	set.resizable = vid->resizable;

	if (vid->mode[0] > 0 && vid->mode[1] > 0) {
		set.mode[0] = vid->mode[0];
		set.mode[1] = vid->mode[1];
	}

	sysVideoApply(sysVid, &set);

	if (vid->wndX >= 0 && vid->wndY >= 0) {
		GLFWwindow *wnd = (GLFWwindow *)sysVideoGetWindowHandle(sysVid);
		if (wnd)
			glfwSetWindowPos(wnd, vid->wndX, vid->wndY);
	}

	if (vid->maximized) {
		GLFWwindow *wnd = (GLFWwindow *)sysVideoGetWindowHandle(sysVid);
		if (wnd)
			glfwMaximizeWindow(wnd);
	}
}

void coreVideoSave(coreVideo_t *vid, sys_video_t *sysVid, coreConfig_t *cfg)
{
	GLFWwindow *wnd = (GLFWwindow *)sysVideoGetWindowHandle(sysVid);

	if (wnd) {
		int mx = glfwGetWindowAttrib(wnd, GLFW_MAXIMIZED);
		vid->maximized = mx;

		if (!mx) {
			glfwGetWindowPos(wnd, &vid->wndX, &vid->wndY);
			glfwGetWindowSize(wnd, &vid->wndW, &vid->wndH);
		}
	}

	char buf[64];

	snprintf(buf, sizeof(buf), "%d", vid->wndX);
	coreConfigSetString(cfg, "vid_wnd_x", buf);

	snprintf(buf, sizeof(buf), "%d", vid->wndY);
	coreConfigSetString(cfg, "vid_wnd_y", buf);

	snprintf(buf, sizeof(buf), "%d", vid->wndW);
	coreConfigSetString(cfg, "vid_wnd_w", buf);

	snprintf(buf, sizeof(buf), "%d", vid->wndH);
	coreConfigSetString(cfg, "vid_wnd_h", buf);

	snprintf(buf, sizeof(buf), "%d", vid->maximized);
	coreConfigSetString(cfg, "vid_maximized", buf);

	snprintf(buf, sizeof(buf), "%d", vid->fullscreen);
	coreConfigSetString(cfg, "vid_fullscreen", buf);

	snprintf(buf, sizeof(buf), "%d", vid->display);
	coreConfigSetString(cfg, "vid_display", buf);

	snprintf(buf, sizeof(buf), "%d", vid->resizable);
	coreConfigSetString(cfg, "vid_resizable", buf);
}
