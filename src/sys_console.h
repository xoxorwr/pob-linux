#ifndef SG_SYS_CONSOLE_H
#define SG_SYS_CONSOLE_H

#include "common.h"

typedef struct console_s {
	int initialised;
} console_t;

void sysConsoleInit(console_t *con);
void sysConsoleShutdown(console_t *con);
void sysConsolePrintf(console_t *con, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void sysConsoleWarning(console_t *con, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif
