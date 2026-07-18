#include "console.h"

#define CMD_BUFFER_SIZE 4096
#define CMD_MAX_LEN     256

static conVar_t*        s_cvarHead   = NULL;
static conCmd_t*        s_cmdHead    = NULL;
static conPrintHook_t   s_printHook  = NULL;

static char  s_cmdBuffer[CMD_BUFFER_SIZE];
static int   s_cmdBufferLen = 0;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static void StripNewline(char* str) {
	size_t len = strlen(str);
	while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
		str[--len] = '\0';
	}
}

static conCmd_t* FindCmd(const char* name) {
	conCmd_t* cmd = s_cmdHead;
	while (cmd) {
		if (_stricmp(cmd->name, name) == 0)
			return cmd;
		cmd = cmd->next;
	}
	return NULL;
}

static int Tokenize(const char* in, char out[][CMD_MAX_LEN], int maxTokens) {
	int   count = 0;
	const char* p = in;

	while (*p && count < maxTokens) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;

		int len = 0;
		while (*p && *p != ' ' && *p != '\t' && len < CMD_MAX_LEN - 1) {
			out[count][len++] = *p++;
		}
		out[count][len] = '\0';
		count++;
	}
	return count;
}

/* ------------------------------------------------------------------ */
/* Print functions                                                     */
/* ------------------------------------------------------------------ */

static void InternalPrint(FILE* dest, const char* fmt, va_list args) {
	char buf[2048];
	vsnprintf(buf, sizeof(buf), fmt, args);
	StripNewline(buf);

	if (s_printHook)
		s_printHook(buf);

	fprintf(dest, "%s\n", buf);
}

void conPrintf(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	InternalPrint(stderr, fmt, args);
	va_end(args);
}

void conWarning(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	InternalPrint(stderr, fmt, args);
	va_end(args);
}

void conPrintFunc(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	InternalPrint(stderr, fmt, args);
	va_end(args);
}

/* ------------------------------------------------------------------ */
/* CVars                                                               */
/* ------------------------------------------------------------------ */

conVar_t* conCvar_Add(const char* name, const char* value, int flags, float minVal, float maxVal) {
	conVar_t* existing = conCvar_Find(name);
	if (existing)
		return existing;

	conVar_t* cv = (conVar_t*)calloc(1, sizeof(conVar_t));
	if (!cv)
		return NULL;

	strncpy(cv->name, name, sizeof(cv->name) - 1);
	cv->name[sizeof(cv->name) - 1] = '\0';
	strncpy(cv->string, value, sizeof(cv->string) - 1);
	cv->string[sizeof(cv->string) - 1] = '\0';
	cv->flags   = flags;
	cv->minVal  = minVal;
	cv->maxVal  = maxVal;

	cv->floatVal = (float)atof(value);
	cv->intVal   = atoi(value);

	if ((flags & CV_CLAMP) && maxVal > minVal) {
		if (cv->floatVal < minVal) cv->floatVal = minVal;
		if (cv->floatVal > maxVal) cv->floatVal = maxVal;
		if (cv->intVal < (int)minVal) cv->intVal = (int)minVal;
		if (cv->intVal > (int)maxVal) cv->intVal = (int)maxVal;
	}

	cv->next    = s_cvarHead;
	s_cvarHead  = cv;

	return cv;
}

int conCvar_Set(const char* name, const char* value) {
	conVar_t* cv = conCvar_Find(name);
	if (!cv)
		return 0;

	strncpy(cv->string, value, sizeof(cv->string) - 1);
	cv->string[sizeof(cv->string) - 1] = '\0';

	cv->floatVal = (float)atof(value);
	cv->intVal   = atoi(value);

	if ((cv->flags & CV_CLAMP) && cv->maxVal > cv->minVal) {
		if (cv->floatVal < cv->minVal) cv->floatVal = cv->minVal;
		if (cv->floatVal > cv->maxVal) cv->floatVal = cv->maxVal;
		if (cv->intVal < (int)cv->minVal) cv->intVal = (int)cv->minVal;
		if (cv->intVal > (int)cv->maxVal) cv->intVal = (int)cv->maxVal;
	}

	return 1;
}

conVar_t* conCvar_Find(const char* name) {
	conVar_t* cv = s_cvarHead;
	while (cv) {
		if (_stricmp(cv->name, name) == 0)
			return cv;
		cv = cv->next;
	}
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

conCmd_t* conCmd_Add(const char* name, const char* description, conCmdHandler_t handler, void* ctx) {
	conCmd_t* existing = FindCmd(name);
	if (existing)
		return existing;

	conCmd_t* cmd = (conCmd_t*)calloc(1, sizeof(conCmd_t));
	if (!cmd)
		return NULL;

	strncpy(cmd->name, name, sizeof(cmd->name) - 1);
	cmd->name[sizeof(cmd->name) - 1] = '\0';
	strncpy(cmd->description, description, sizeof(cmd->description) - 1);
	cmd->description[sizeof(cmd->description) - 1] = '\0';
	cmd->handler = handler;
	cmd->ctx     = ctx;

	cmd->next  = s_cmdHead;
	s_cmdHead  = cmd;

	return cmd;
}

void conCmd_Remove(const char* name) {
	conCmd_t** pp = &s_cmdHead;
	while (*pp) {
		if (_stricmp((*pp)->name, name) == 0) {
			conCmd_t* tmp = *pp;
			*pp = tmp->next;
			free(tmp);
			return;
		}
		pp = &(*pp)->next;
	}
}

int conCmd_Exec(int argc, char** argv) {
	if (argc < 1)
		return 0;

	conCmd_t* cmd = FindCmd(argv[0]);
	if (!cmd || !cmd->handler)
		return 0;

	cmd->handler(cmd->ctx, argc, argv);
	return 1;
}

void conCmd_Buffer(const char* cmd) {
	size_t cmdLen = strlen(cmd);
	if (cmdLen == 0)
		return;

	if (s_cmdBufferLen + (int)cmdLen + 2 > CMD_BUFFER_SIZE) {
		conWarning("conCmd_Buffer: command buffer overflow");
		return;
	}

	memcpy(s_cmdBuffer + s_cmdBufferLen, cmd, cmdLen);
	s_cmdBufferLen += (int)cmdLen;
	s_cmdBuffer[s_cmdBufferLen++] = '\n';
	s_cmdBuffer[s_cmdBufferLen]   = '\0';
}

void conExecCommands(void) {
	if (s_cmdBufferLen == 0)
		return;

	char localBuf[CMD_BUFFER_SIZE];
	memcpy(localBuf, s_cmdBuffer, s_cmdBufferLen);
	localBuf[s_cmdBufferLen] = '\0';
	s_cmdBufferLen = 0;

	char line[CMD_MAX_LEN];
	int  pos = 0;

	for (int i = 0; localBuf[i]; i++) {
		if (localBuf[i] == '\n') {
			line[pos] = '\0';
			StripNewline(line);

			if (pos > 0) {
				char tokens[16][CMD_MAX_LEN];
				int  argc = Tokenize(line, tokens, 16);
				if (argc > 0)
					conCmd_Exec(argc, (char**)tokens);
			}
			pos = 0;
		} else {
			if (pos < CMD_MAX_LEN - 1)
				line[pos++] = localBuf[i];
		}
	}

	if (pos > 0) {
		line[pos] = '\0';
		StripNewline(line);
		char tokens[16][CMD_MAX_LEN];
		int  argc = Tokenize(line, tokens, 16);
		if (argc > 0)
			conCmd_Exec(argc, (char**)tokens);
	}
}

/* ------------------------------------------------------------------ */
/* Print hook                                                          */
/* ------------------------------------------------------------------ */

void conInstallPrintHook(conPrintHook_t hook) {
	s_printHook = hook;
}

/* ------------------------------------------------------------------ */
/* Init / Shutdown                                                     */
/* ------------------------------------------------------------------ */

void conInit(void) {
	s_cvarHead       = NULL;
	s_cmdHead        = NULL;
	s_printHook      = NULL;
	s_cmdBufferLen   = 0;
}

void conShutdown(void) {
	conVar_t* cv = s_cvarHead;
	while (cv) {
		conVar_t* next = cv->next;
		free(cv);
		cv = next;
	}
	s_cvarHead = NULL;

	conCmd_t* cmd = s_cmdHead;
	while (cmd) {
		conCmd_t* next = cmd->next;
		free(cmd);
		cmd = next;
	}
	s_cmdHead = NULL;

	s_cmdBufferLen  = 0;
	s_printHook     = NULL;
}
