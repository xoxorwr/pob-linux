#include "sys_opengl.h"
#include "sys_main.h"
#include "sys_video.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

void sysGLInit(sys_opengl_t *gl, sys_main_t *sys, sys_glSet_s *set)
{
	gl->sys = sys;
	gl->initialised = 1;
	glfwSwapInterval(set->vsync ? 1 : 0);
}

void sysGLShutdown(sys_opengl_t *gl)
{
	gl->initialised = 0;
}

void sysGLSwap(sys_opengl_t *gl)
{
	GLFWwindow *wnd = (GLFWwindow *)sysVideoGetWindowHandle(gl->sys->video);
	if (wnd) {
		glfwSwapBuffers(wnd);
	}
}

void *sysGLGetProc(const char *name)
{
	return (void *)glfwGetProcAddress(name);
}
