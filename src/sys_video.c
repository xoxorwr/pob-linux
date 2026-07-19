#include "sys_video.h"
#include "sys_main.h"
#include "core_image.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gles2.h>

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static int glfwKeyToSG(int key, int scancode)
{
	switch (key) {
	case GLFW_KEY_BACKSPACE:     return KEY_BACK;
	case GLFW_KEY_TAB:           return KEY_TAB;
	case GLFW_KEY_ENTER:         return KEY_RETURN;
	case GLFW_KEY_ESCAPE:        return KEY_ESCAPE;
	case GLFW_KEY_LEFT_SHIFT:    return KEY_SHIFT;
	case GLFW_KEY_RIGHT_SHIFT:   return KEY_SHIFT;
	case GLFW_KEY_LEFT_CONTROL:  return KEY_CTRL;
	case GLFW_KEY_RIGHT_CONTROL: return KEY_CTRL;
	case GLFW_KEY_LEFT_ALT:      return KEY_ALT;
	case GLFW_KEY_RIGHT_ALT:     return KEY_ALT;
	case GLFW_KEY_PAUSE:         return KEY_PAUSE;
	case GLFW_KEY_PAGE_UP:       return KEY_PGUP;
	case GLFW_KEY_PAGE_DOWN:     return KEY_PGDN;
	case GLFW_KEY_END:           return KEY_END;
	case GLFW_KEY_HOME:          return KEY_HOME;
	case GLFW_KEY_LEFT:          return KEY_LEFT;
	case GLFW_KEY_UP:            return KEY_UP;
	case GLFW_KEY_RIGHT:         return KEY_RIGHT;
	case GLFW_KEY_DOWN:          return KEY_DOWN;
	case GLFW_KEY_PRINT_SCREEN:  return KEY_PRINTSCRN;
	case GLFW_KEY_INSERT:        return KEY_INSERT;
	case GLFW_KEY_DELETE:        return KEY_DELETE;
	case GLFW_KEY_NUM_LOCK:      return KEY_NUMLOCK;
	case GLFW_KEY_SCROLL_LOCK:   return KEY_SCROLL;
	case GLFW_KEY_KP_ENTER:      return KEY_RETURN;
	case GLFW_KEY_SPACE:         return ' ';
	case GLFW_KEY_SEMICOLON:     return ';';
	case GLFW_KEY_EQUAL:         return '+';
	case GLFW_KEY_COMMA:         return ',';
	case GLFW_KEY_MINUS:         return '-';
	case GLFW_KEY_PERIOD:        return '.';
	case GLFW_KEY_SLASH:         return '/';
	case GLFW_KEY_GRAVE_ACCENT:  return '`';
	case GLFW_KEY_LEFT_BRACKET:  return '[';
	case GLFW_KEY_BACKSLASH:     return '\\';
	case GLFW_KEY_RIGHT_BRACKET: return ']';
	case GLFW_KEY_APOSTROPHE:    return '\'';
	case GLFW_KEY_KP_0:          return '0';
	case GLFW_KEY_KP_SUBTRACT:   return '-';
	case GLFW_KEY_KP_ADD:        return '+';
	default:
		break;
	}

	if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F15)
		return KEY_F1 + (key - GLFW_KEY_F1);

	if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
		return '0' + (key - GLFW_KEY_0);

	if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) {
		const char *name = glfwGetKeyName(key, scancode);
		if (name && name[0] && !name[1])
			return (unsigned char)tolower((unsigned char)name[0]);
		return 'a' + (key - GLFW_KEY_A);
	}

	return 0;
}

static char glfwKeyExtraChar(int key)
{
	switch (key) {
	case GLFW_KEY_BACKSPACE: return 0x08;
	case GLFW_KEY_TAB:       return 0x09;
	case GLFW_KEY_ENTER:     return 0x0d;
	case GLFW_KEY_ESCAPE:    return 0x1b;
	default: break;
	}
	return 0;
}

static void cbCursorEnter(GLFWwindow *wnd, int entered)
{
	sys_video_t *vid = (sys_video_t *)glfwGetWindowUserPointer(wnd);
	vid->cursorInWindow = !!entered;
}

static void cbMouseButton(GLFWwindow *wnd, int button, int action, int mods)
{
	(void)mods;
	sys_video_t *vid = (sys_video_t *)glfwGetWindowUserPointer(wnd);
	int sgKey;
	switch (button) {
	case GLFW_MOUSE_BUTTON_LEFT:   sgKey = KEY_LMOUSE; break;
	case GLFW_MOUSE_BUTTON_MIDDLE: sgKey = KEY_MMOUSE; break;
	case GLFW_MOUSE_BUTTON_RIGHT:  sgKey = KEY_RMOUSE; break;
	case GLFW_MOUSE_BUTTON_4:      sgKey = KEY_MOUSE4; break;
	case GLFW_MOUSE_BUTTON_5:      sgKey = KEY_MOUSE5; break;
	default: return;
	}
	int isDown = (action == GLFW_PRESS);
	int eventType = isDown ? KE_KEYDOWN : KE_KEYUP;

	g_sys.heldKeyState[sgKey] = isDown;

	if (isDown && vid->keyEvent) {
		double xpos, ypos;
		glfwGetCursorPos(wnd, &xpos, &ypos);
		int cx = (int)floor(xpos);
		int cy = (int)floor(ypos);
		if (vid->lastClickKey == sgKey &&
			abs(vid->lastClickX - cx) <= 5 &&
			abs(vid->lastClickY - cy) <= 5) {
			eventType = KE_DBLCLK;
			vid->lastClickKey = -1;
		} else {
			vid->lastClickKey = sgKey;
			vid->lastClickX = cx;
			vid->lastClickY = cy;
		}
	}

	if (vid->keyEvent)
		vid->keyEvent(vid->eventUserData, sgKey, eventType);
}

static void cbScroll(GLFWwindow *wnd, double xoffset, double yoffset)
{
	(void)xoffset;
	sys_video_t *vid = (sys_video_t *)glfwGetWindowUserPointer(wnd);
	if (!vid->keyEvent) return;
	if (yoffset > 0) {
		vid->keyEvent(vid->eventUserData, KEY_MWHEELUP, KE_KEYDOWN);
		vid->keyEvent(vid->eventUserData, KEY_MWHEELUP, KE_KEYUP);
	} else if (yoffset < 0) {
		vid->keyEvent(vid->eventUserData, KEY_MWHEELDOWN, KE_KEYDOWN);
		vid->keyEvent(vid->eventUserData, KEY_MWHEELDOWN, KE_KEYUP);
	}
}

static void cbKey(GLFWwindow *wnd, int key, int scancode, int action, int mods)
{
	(void)mods;
	sys_video_t *vid = (sys_video_t *)glfwGetWindowUserPointer(wnd);
	if (!vid->keyEvent) return;

	int sgKey = glfwKeyToSG(key, scancode);
	if (!sgKey) return;

	int isDown = (action == GLFW_PRESS || action == GLFW_REPEAT);
	g_sys.heldKeyState[sgKey] = isDown;
	vid->keyEvent(vid->eventUserData, sgKey, isDown ? KE_KEYDOWN : KE_KEYUP);

	if (isDown) {
		char ch = glfwKeyExtraChar(key);
		if (ch && vid->charEvent) {
			vid->charEvent(vid->eventUserData, (unsigned int)ch);
		}
	}
}

static void cbChar(GLFWwindow *wnd, unsigned int codepoint)
{
	sys_video_t *vid = (sys_video_t *)glfwGetWindowUserPointer(wnd);
	if (vid->charEvent)
		vid->charEvent(vid->eventUserData, codepoint);
}

static void cbFramebufferSize(GLFWwindow *wnd, int width, int height)
{
	sys_video_t *vid = (sys_video_t *)glfwGetWindowUserPointer(wnd);
	if (!glfwGetWindowAttrib(wnd, GLFW_ICONIFIED)) {
		vid->scrSize[0] = width;
		vid->scrSize[1] = height;
	}
	sysVideoFramebufferSizeChanged(vid, width, height);
}

static void cbWindowSize(GLFWwindow *wnd, int width, int height)
{
	sys_video_t *vid = (sys_video_t *)glfwGetWindowUserPointer(wnd);
	int maximized = glfwGetWindowAttrib(wnd, GLFW_MAXIMIZED);
	sysVideoSizeChanged(vid, width, height, maximized);
}

static void cbWindowPos(GLFWwindow *wnd, int x, int y)
{
	sys_video_t *vid = (sys_video_t *)glfwGetWindowUserPointer(wnd);
	sysVideoPosChanged(vid, x, y);
}

static void cbWindowIconify(GLFWwindow *wnd, int iconified)
{
	(void)wnd;
	g_sys.minimized = iconified;
}

void sysVideoInit(sys_video_t *vid, sys_main_t *sys)
{
	memset(vid, 0, sizeof(*vid));
	vid->sys = sys;
	vid->lastClickKey = -1;
	strncpy(vid->curTitle, CFG_TITLE, sizeof(vid->curTitle) - 1);
}

void sysVideoShutdown(sys_video_t *vid)
{
	if (vid->wnd) {
		glfwDestroyWindow((GLFWwindow *)vid->wnd);
		vid->wnd = NULL;
	}
	glfwTerminate();
	vid->initialised = 0;
}

int sysVideoApply(sys_video_t *vid, sys_vidSet_s *set)
{
	vid->cur = *set;

	if (!vid->initialised) {
		glfwInitHint(GLFW_ANGLE_PLATFORM_TYPE, GLFW_ANGLE_PLATFORM_TYPE_NONE);
		glfwInit();

		glfwWindowHint(GLFW_RESIZABLE, vid->cur.resizable ? GLFW_TRUE : GLFW_FALSE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
		glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

		GLFWmonitor **monitors = glfwGetMonitors(&vid->monInfo.numMon);
		if (vid->monInfo.numMon > 16) vid->monInfo.numMon = 16;
		for (int i = 0; i < vid->monInfo.numMon; i++) {
			vid->monInfo.mon[i].hnd = monitors[i];
			glfwGetMonitorPos(monitors[i], &vid->monInfo.mon[i].left, &vid->monInfo.mon[i].top);
			GLFWvidmode const *mode = glfwGetVideoMode(monitors[i]);
			vid->monInfo.mon[i].width = mode->width;
			vid->monInfo.mon[i].height = mode->height;
		}
		vid->monInfo.priMon = 0;

		int display = 0;
		vid->scrSize[0] = vid->monInfo.mon[display].width;
		vid->scrSize[1] = vid->monInfo.mon[display].height;

		if (vid->cur.mode[0] == 0) {
			vid->cur.mode[0] = vid->scrSize[0];
			vid->cur.mode[1] = vid->scrSize[1];
		}

		int wx = (vid->scrSize[0] - vid->cur.mode[0]) / 2 + vid->monInfo.mon[display].left;
		int wy = (vid->scrSize[1] - vid->cur.mode[1]) / 2 + vid->monInfo.mon[display].top;

		vid->wnd = glfwCreateWindow(vid->cur.mode[0], vid->cur.mode[1], vid->curTitle, NULL, NULL);
		if (!vid->wnd) {
			const char *errDesc = "Unknown error";
			glfwGetError(&errDesc);
			fprintf(stderr, "Could not create window: %s\n", errDesc);
			glfwTerminate();
			return -1;
		}

		glfwMakeContextCurrent((GLFWwindow *)vid->wnd);
		gladLoadGLES2((void *)glfwGetProcAddress);

		glfwGetFramebufferSize((GLFWwindow *)vid->wnd, &vid->scrSize[0], &vid->scrSize[1]);

		glfwSetWindowUserPointer((GLFWwindow *)vid->wnd, vid);
		glfwSetCursorEnterCallback((GLFWwindow *)vid->wnd, cbCursorEnter);
		glfwSetMouseButtonCallback((GLFWwindow *)vid->wnd, cbMouseButton);
		glfwSetScrollCallback((GLFWwindow *)vid->wnd, cbScroll);
		glfwSetKeyCallback((GLFWwindow *)vid->wnd, cbKey);
		glfwSetCharCallback((GLFWwindow *)vid->wnd, cbChar);
		glfwSetFramebufferSizeCallback((GLFWwindow *)vid->wnd, cbFramebufferSize);
		glfwSetWindowSizeCallback((GLFWwindow *)vid->wnd, cbWindowSize);
		glfwSetWindowPosCallback((GLFWwindow *)vid->wnd, cbWindowPos);
		glfwSetWindowIconifyCallback((GLFWwindow *)vid->wnd, cbWindowIconify);

		glfwSetWindowSizeLimits((GLFWwindow *)vid->wnd,
			vid->minSize[0], vid->minSize[1],
			GLFW_DONT_CARE, GLFW_DONT_CARE);

		/* Set window icon */
		{
			char iconPath[1024];
			snprintf(iconPath, sizeof(iconPath), "%s/icon.png", vid->sys->basePath);
			coreImage_t img;
			if (coreImageLoad(&img, iconPath, 4)) {
				GLFWimage glfwImg;
				glfwImg.width = img.width;
				glfwImg.height = img.height;
				glfwImg.pixels = img.data;
				glfwSetWindowIcon((GLFWwindow *)vid->wnd, 1, &glfwImg);
				coreImageFree(&img);
			}
		}

		glfwSetWindowPos((GLFWwindow *)vid->wnd, wx, wy);
		glfwShowWindow((GLFWwindow *)vid->wnd);
		vid->initialised = 1;
	} else {
		GLFWmonitor **monitors = glfwGetMonitors(&vid->monInfo.numMon);
		if (vid->monInfo.numMon > 16) vid->monInfo.numMon = 16;
		for (int i = 0; i < vid->monInfo.numMon; i++) {
			vid->monInfo.mon[i].hnd = monitors[i];
			glfwGetMonitorPos(monitors[i], &vid->monInfo.mon[i].left, &vid->monInfo.mon[i].top);
			GLFWvidmode const *mode = glfwGetVideoMode(monitors[i]);
			vid->monInfo.mon[i].width = mode->width;
			vid->monInfo.mon[i].height = mode->height;
		}
		vid->monInfo.priMon = 0;

		int display = 0;
		vid->scrSize[0] = vid->monInfo.mon[display].width;
		vid->scrSize[1] = vid->monInfo.mon[display].height;

		int wx = (vid->scrSize[0] - vid->cur.mode[0]) / 2 + vid->monInfo.mon[display].left;
		int wy = (vid->scrSize[1] - vid->cur.mode[1]) / 2 + vid->monInfo.mon[display].top;
		glfwSetWindowSize((GLFWwindow *)vid->wnd, vid->cur.mode[0], vid->cur.mode[1]);
		glfwSetWindowPos((GLFWwindow *)vid->wnd, wx, wy);
		if (vid->cur.shown) {
			glfwShowWindow((GLFWwindow *)vid->wnd);
		} else {
			glfwHideWindow((GLFWwindow *)vid->wnd);
		}
	}

	return 0;
}

void sysVideoSetForeground(sys_video_t *vid)
{
	if (vid->initialised && vid->wnd)
		glfwFocusWindow((GLFWwindow *)vid->wnd);
}

int sysVideoIsActive(sys_video_t *vid)
{
	if (!vid->wnd) return 0;
	return glfwGetWindowAttrib((GLFWwindow *)vid->wnd, GLFW_FOCUSED);
}

void sysVideoSetVisible(sys_video_t *vid, int vis)
{
	if (!vid->initialised || !vid->wnd) return;
	if (vis)
		glfwShowWindow((GLFWwindow *)vid->wnd);
	else
		glfwHideWindow((GLFWwindow *)vid->wnd);
}

void sysVideoSetTitle(sys_video_t *vid, const char *title)
{
	strncpy(vid->curTitle, (title && *title) ? title : CFG_TITLE, sizeof(vid->curTitle) - 1);
	vid->curTitle[sizeof(vid->curTitle) - 1] = '\0';
	if (vid->initialised && vid->wnd)
		glfwSetWindowTitle((GLFWwindow *)vid->wnd, vid->curTitle);
}

void *sysVideoGetWindowHandle(sys_video_t *vid)
{
	return vid->wnd;
}

void sysVideoGetRelativeCursor(sys_video_t *vid, int *x, int *y)
{
	if (!vid->initialised || !vid->wnd) { *x = 0; *y = 0; return; }
	double xpos, ypos;
	glfwGetCursorPos((GLFWwindow *)vid->wnd, &xpos, &ypos);

	int winW, winH;
	int fbW, fbH;
	glfwGetWindowSize((GLFWwindow *)vid->wnd, &winW, &winH);
	glfwGetFramebufferSize((GLFWwindow *)vid->wnd, &fbW, &fbH);

	if (winW > 0 && winH > 0) {
		xpos = xpos * (double)fbW / (double)winW;
		ypos = ypos * (double)fbH / (double)winH;
	}

	*x = (int)floor(xpos);
	*y = (int)floor(ypos);
}

void sysVideoSetRelativeCursor(sys_video_t *vid, int x, int y)
{
	if (!vid->initialised || !vid->wnd) return;
	double xpos = x;
	double ypos = y;

	int winW, winH;
	int fbW, fbH;
	glfwGetWindowSize((GLFWwindow *)vid->wnd, &winW, &winH);
	glfwGetFramebufferSize((GLFWwindow *)vid->wnd, &fbW, &fbH);

	if (fbW > 0 && fbH > 0) {
		xpos = xpos * (double)winW / (double)fbW;
		ypos = ypos * (double)winH / (double)fbH;
	}

	glfwSetCursorPos((GLFWwindow *)vid->wnd, xpos, ypos);
}

int sysVideoIsCursorOverWindow(sys_video_t *vid)
{
	if (vid->initialised && vid->wnd)
		return vid->cursorInWindow;
	return 1;
}

void sysVideoFramebufferSizeChanged(sys_video_t *vid, int width, int height)
{
	if (vid->initialised && vid->wnd &&
		!glfwGetWindowAttrib((GLFWwindow *)vid->wnd, GLFW_ICONIFIED)) {
		vid->scrSize[0] = width;
		vid->scrSize[1] = height;
	}
}

void sysVideoSizeChanged(sys_video_t *vid, int width, int height, int maximized)
{
	(void)vid;
	(void)width;
	(void)height;
	(void)maximized;
}

void sysVideoPosChanged(sys_video_t *vid, int x, int y)
{
	(void)vid;
	(void)x;
	(void)y;
}

void sysVideoGetMinSize(sys_video_t *vid, int *width, int *height)
{
	*width = vid->minSize[0];
	*height = vid->minSize[1];
}
