#include "sys_main.h"
#include "sys_video.h"
#include "sys_opengl.h"
#include "sys_console.h"
#include "core_main.h"
#include "ui_main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <pthread.h>
#include <fnmatch.h>

#include <GLFW/glfw3.h>

sys_main_t g_sys;

static void findBasePath(void)
{
	char buf[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (len == -1) {
		buf[0] = '\0';
	} else {
		buf[len] = '\0';
	}
	char *slash = strrchr(buf, '/');
	if (slash) {
		*slash = '\0';
	}
	strncpy(g_sys.basePath, buf, sizeof(g_sys.basePath) - 1);
	g_sys.basePath[sizeof(g_sys.basePath) - 1] = '\0';
}

static void findUserPath(void)
{
	const char *xdg = getenv("XDG_DATA_HOME");
	if (xdg && *xdg) {
		strncpy(g_sys.userPath, xdg, sizeof(g_sys.userPath) - 1);
		g_sys.userPath[sizeof(g_sys.userPath) - 1] = '\0';
		return;
	}
	const char *home = getenv("HOME");
	if (home && *home) {
		snprintf(g_sys.userPath, sizeof(g_sys.userPath), "%s/.local/share", home);
		return;
	}
	struct passwd *pw = getpwuid(getuid());
	if (pw && pw->pw_dir) {
		snprintf(g_sys.userPath, sizeof(g_sys.userPath), "%s/.local/share", pw->pw_dir);
		return;
	}
	g_sys.userPath[0] = '\0';
}

static void coreKeyEvent(void *userData, int key, int type)
{
	core_main_t *core = (core_main_t *)userData;
	if (core && core->keyEvent)
		core->keyEvent(core, key, type);
}

static void coreCharEvent(void *userData, unsigned int codepoint)
{
	core_main_t *core = (core_main_t *)userData;
	if (core && core->keyEvent)
		core->keyEvent(core, (int)codepoint, KE_CHAR);
}

/* ======== Core vtable callbacks ======== */

static void bridgeInit(core_main_t *cm, int argc, char **argv)
{
	core_t *core = (core_t *)cm->data;
	coreInit(core, &g_sys, argc, argv);

	ui_main_t *ui = calloc(1, sizeof(ui_main_t));
	core->uiData = ui;
	core->uiFrame = (void (*)(void *))uiFrame;
	uiInit(ui, &g_sys, cm, argc, argv);
}

static void bridgeFrame(core_main_t *cm)
{
	core_t *core = (core_t *)cm->data;
	coreFrame(core);
}

static void bridgeShutdown(core_main_t *cm)
{
	core_t *core = (core_t *)cm->data;
	if (core->uiData) {
		uiShutdown((ui_main_t *)core->uiData);
		free(core->uiData);
		core->uiData = NULL;
	}
	coreShutdown(core);
}

static void bridgeKeyEvent(core_main_t *cm, int key, int type)
{
	core_t *core = (core_t *)cm->data;
	if (core->uiData)
		uiKeyEvent((ui_main_t *)core->uiData, key, type);
}

void sysInit(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	memset(&g_sys, 0, sizeof(g_sys));
	g_sys.x64 = (sizeof(void *) == 8);
#ifdef NDEBUG
	g_sys.debug = 0;
#else
	g_sys.debug = 1;
#endif
	g_sys.processorCount = sysconf(_SC_NPROCESSORS_ONLN);
	if (g_sys.processorCount < 1)
		g_sys.processorCount = 1;

	findBasePath();
	findUserPath();

	gettimeofday(&g_sys.baseTime, NULL);
}

void sysShutdown(void)
{
	FreeString(g_sys.exitMsg);
	g_sys.exitMsg = NULL;
	FreeString(g_sys.threadError);
	g_sys.threadError = NULL;
}

int sysRun(int argc, char **argv)
{
	g_sys.initialised = 0;
	g_sys.exitFlag = 0;
	g_sys.restartFlag = 0;
	g_sys.exitMsg = NULL;
	g_sys.threadError = NULL;
	g_sys.errorRaised = 0;

	memset(g_sys.heldKeyState, 0, sizeof(g_sys.heldKeyState));

	sysInit(argc, argv);

	g_sys.con = calloc(1, sizeof(console_t));
	sysConsoleInit(g_sys.con);

	g_sys.video = calloc(1, sizeof(sys_video_t));
	sysVideoInit(g_sys.video, &g_sys);

	g_sys.gl = calloc(1, sizeof(sys_opengl_t));
	g_sys.gl->sys = &g_sys;

	g_sys.con->initialised = 1;

	sysConsolePrintf(g_sys.con, CFG_VERSION " %s %s, built " __DATE__ "\n",
		g_sys.x64 ? "x64" : "x86", g_sys.debug ? "Debug" : "Release");

	/* Create core */
	core_t *core = calloc(1, sizeof(core_t));
	g_sys.core = calloc(1, sizeof(core_main_t));
	g_sys.core->data = core;
	g_sys.core->init = bridgeInit;
	g_sys.core->frame = bridgeFrame;
	g_sys.core->shutdown = bridgeShutdown;
	g_sys.core->keyEvent = bridgeKeyEvent;

	g_sys.initialised = 1;

	g_sys.video->keyEvent = coreKeyEvent;
	g_sys.video->charEvent = coreCharEvent;
	g_sys.video->eventUserData = g_sys.core;

	if (g_sys.core && g_sys.core->init)
		g_sys.core->init(g_sys.core, argc, argv);

	while (!g_sys.exitFlag) {
		if (g_sys.minimized) {
			glfwWaitEventsTimeout(0.1);
		} else {
			glfwPollEvents();
		}

		GLFWwindow *wnd = (GLFWwindow *)sysVideoGetWindowHandle(g_sys.video);
		if (wnd && glfwWindowShouldClose(wnd)) {
			sysExit(NULL);
			break;
		}

		if (g_sys.core && g_sys.core->frame)
			g_sys.core->frame(g_sys.core);

		if (g_sys.threadError) {
			sysError("%s", g_sys.threadError);
		}
	}

	if (g_sys.core && g_sys.core->shutdown)
		g_sys.core->shutdown(g_sys.core);

	free(g_sys.core->data);
	free(g_sys.core);
	g_sys.core = NULL;

	if (g_sys.exitMsg) {
		g_sys.exitFlag = 0;
		sysVideoSetVisible(g_sys.video, 0);
		if (g_sys.exitMsg) {
			sysConsolePrintf(g_sys.con, "\n%s", g_sys.exitMsg);
			FreeString(g_sys.exitMsg);
			g_sys.exitMsg = NULL;
		}
		while (!g_sys.exitFlag) {
			sysSleep(50);
		}
	}

	g_sys.initialised = 0;

	sysGLShutdown(g_sys.gl);
	free(g_sys.gl);
	g_sys.gl = NULL;

	sysVideoShutdown(g_sys.video);
	free(g_sys.video);
	g_sys.video = NULL;

	sysConsoleShutdown(g_sys.con);
	free(g_sys.con);
	g_sys.con = NULL;

	sysShutdown();

	return g_sys.restartFlag;
}

int sysGetTime(void)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	int sec = (int)(now.tv_sec - g_sys.baseTime.tv_sec);
	int usec = (int)(now.tv_usec - g_sys.baseTime.tv_usec);
	return sec * 1000 + usec / 1000;
}

void sysSleep(int msec)
{
	struct timespec ts;
	ts.tv_sec = msec / 1000;
	ts.tv_nsec = (msec % 1000) * 1000000L;
	nanosleep(&ts, NULL);
}

int sysIsKeyDown(byte key)
{
	if (key <= KEY_SCROLL)
		return !!g_sys.heldKeyState[key];
	return 0;
}

void sysClipboardCopy(const char *str)
{
	GLFWwindow *wnd = (GLFWwindow *)sysVideoGetWindowHandle(g_sys.video);
	if (wnd)
		glfwSetClipboardString(wnd, str);
}

char *sysClipboardPaste(void)
{
	GLFWwindow *wnd = (GLFWwindow *)sysVideoGetWindowHandle(g_sys.video);
	if (!wnd) return NULL;
	const char *str = glfwGetClipboardString(wnd);
	if (!str) return NULL;
	return AllocString(str);
}

int sysSetWorkDir(const char *path)
{
	if (!path || !*path)
		return chdir(g_sys.basePath);
	return chdir(path);
}

void sysSpawnProcess(const char *cmd, const char *args)
{
	pid_t pid = fork();
	if (pid == 0) {
		if (args && *args) {
			execlp(cmd, cmd, args, (char *)NULL);
		} else {
			execlp(cmd, cmd, (char *)NULL);
		}
		_exit(127);
	}
}

void sysOpenURLErr(const char *url)
{
	pid_t pid = fork();
	if (pid == 0) {
		execlp("xdg-open", "xdg-open", url, (char *)NULL);
		_exit(127);
	}
}

void sysError(const char *fmt, ...)
{
	if (g_sys.errorRaised) return;
	g_sys.errorRaised = 1;

	if (g_sys.initialised && g_sys.video) {
		sysVideoSetVisible(g_sys.video, 0);
	}

	va_list va;
	va_start(va, fmt);
	char *msg = NULL;
	vasprintf(&msg, fmt, va);
	va_end(va);

	fprintf(stderr, "\n--- ERROR ---\n%s\n", msg);
	if (g_sys.con)
		sysConsolePrintf(g_sys.con, "\n--- ERROR ---\n%s", msg);
	free(msg);

	g_sys.exitFlag = 0;
	while (!g_sys.exitFlag) {
		sysSleep(50);
	}

	_exit(0);
}

void sysExit(const char *msg)
{
	if (g_sys.initialised && g_sys.video)
		sysVideoSetVisible(g_sys.video, 0);
	FreeString(g_sys.exitMsg);
	g_sys.exitMsg = msg ? AllocString(msg) : NULL;
	g_sys.exitFlag = 1;
}

void sysRestart(void)
{
	if (g_sys.initialised && g_sys.video)
		sysVideoSetVisible(g_sys.video, 0);
	g_sys.restartFlag = 1;
	FreeString(g_sys.exitMsg);
	g_sys.exitMsg = NULL;
	g_sys.exitFlag = 1;
}
