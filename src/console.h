#ifndef SG_CONSOLE_H
#define SG_CONSOLE_H

#include "common.h"

typedef enum {
	CV_NONE    = 0,
	CV_ARCHIVE = (1 << 0),
	CV_CLAMP   = (1 << 1)
} conVarFlags_t;

typedef struct conVar_s {
	char              name[64];
	char              string[256];
	int               intVal;
	float             floatVal;
	float             minVal;
	float             maxVal;
	int               flags;
	struct conVar_s*  next;
} conVar_t;

typedef void (*conCmdHandler_t)(void* ctx, int argc, char** argv);

typedef struct conCmd_s {
	char                name[64];
	char                description[128];
	conCmdHandler_t     handler;
	void*               ctx;
	struct conCmd_s*    next;
} conCmd_t;

typedef void (*conPrintHook_t)(const char* text);

void conInit(void);
void conShutdown(void);

void conPrintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void conWarning(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void conPrintFunc(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

conVar_t* conCvar_Add(const char* name, const char* value, int flags, float minVal, float maxVal);
int       conCvar_Set(const char* name, const char* value);
conVar_t* conCvar_Find(const char* name);

conCmd_t* conCmd_Add(const char* name, const char* description, conCmdHandler_t handler, void* ctx);
void      conCmd_Remove(const char* name);
int       conCmd_Exec(int argc, char** argv);
void      conCmd_Buffer(const char* cmd);
void      conExecCommands(void);

void conInstallPrintHook(conPrintHook_t hook);

#endif
