#include "sys_console.h"

#include <stdio.h>
#include <stdarg.h>

void sysConsoleInit(console_t *con)
{
	con->initialised = 1;
}

void sysConsoleShutdown(console_t *con)
{
	con->initialised = 0;
}

void sysConsolePrintf(console_t *con, const char *fmt, ...)
{
	(void)con;
	va_list va;
	va_start(va, fmt);
	fprintf(stderr, "[CON] ");
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "\n");
	va_end(va);
}

void sysConsoleWarning(console_t *con, const char *fmt, ...)
{
	(void)con;
	va_list va;
	va_start(va, fmt);
	fprintf(stderr, "[WRN] ");
	vfprintf(stderr, fmt, va);
	fprintf(stderr, "\n");
	va_end(va);
}
