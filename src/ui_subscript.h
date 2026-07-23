#ifndef SG_UI_SUBSCRIPT_H
#define SG_UI_SUBSCRIPT_H

#include "ui_main.h"

/* ======== Sub-script Struct ======== */

struct ui_subscript_s {
	int         id;
	int         isRunning;
	int         finished;
	int         resultsRef;
	int         nresults;
	char       *errorStr;
	ui_main_t  *ui;
};

/* ======== Function Declarations ======== */

int   uiSubScriptLaunch(ui_main_t *ui, const char *scriptText, const char *funcList,
                        const char *subList, int extraArgc);
void  uiSubScriptAbort(ui_main_t *ui, int slot);
int   uiSubScriptIsRunning(ui_main_t *ui, int slot);
void  uiSubScriptFrame(ui_main_t *ui);
void  uiSubScriptFreeAll(ui_main_t *ui);

#endif
