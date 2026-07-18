#include "core_main.h"
#include "sys_main.h"
#include "sys_video.h"
#include "console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char cfgPath[1024];
static char scriptCfgPath[1024];

void coreInit(core_t *core, sys_main_t *sys, int argc, char **argv)
{
	(void)argc;
	(void)argv;
	memset(core, 0, sizeof(*core));
	core->sys = sys;

	conPrintf("Core: initializing config...\n");
	coreConfigInit(&core->config);

	snprintf(cfgPath, sizeof(cfgPath), "%s/" CFG_DATAPATH "SimpleGraphic.cfg", sys->basePath);
	if (coreConfigLoad(&core->config, cfgPath)) {
		conPrintf("Core: loaded %s\n", cfgPath);
	} else {
		conPrintf("Core: %s not found, using defaults\n", cfgPath);
	}

	for (int i = 1; i < argc; i++) {
		if (_stricmp(argv[i], "-cfg") == 0 && i + 1 < argc) {
			i++;
			const char *path = argv[i];
			if (coreConfigLoad(&core->config, path)) {
				conPrintf("Core: loaded script %s\n", path);
				strncpy(scriptCfgPath, path, sizeof(scriptCfgPath) - 1);
				scriptCfgPath[sizeof(scriptCfgPath) - 1] = '\0';
			}
		}
	}

	if (scriptCfgPath[0] == '\0') {
		snprintf(scriptCfgPath, sizeof(scriptCfgPath), "%s/" CFG_DATAPATH "script.cfg", sys->basePath);
		if (coreConfigLoad(&core->config, scriptCfgPath)) {
			conPrintf("Core: loaded %s\n", scriptCfgPath);
		}
	}

	conPrintf("Core: initializing video...\n");
	coreVideoInit(&core->video);

	if (sys->video) {
		coreVideoApply(&core->video, sys->video, &core->config);
	}

	cfgVar_t *v;
	for (v = core->config.vars; v; v = v->next) {
		if (v->flags & CFG_ARCHIVE)
			continue;
		conCvar_Add(v->name, v->stringVal, 0, 0.0f, 0.0f);
	}
}

void coreFrame(core_t *core)
{
	if (core->uiFrame)
		core->uiFrame(core->uiData);
}

void coreShutdown(core_t *core)
{
	conPrintf("Core: saving video state...\n");
	if (core->sys && core->sys->video) {
		coreVideoSave(&core->video, core->sys->video, &core->config);
	}

	coreVideoShutdown(&core->video);

	conPrintf("Core: saving config...\n");
	coreConfigSave(&core->config, cfgPath);

	if (scriptCfgPath[0] != '\0') {
		coreConfigSave(&core->config, scriptCfgPath);
	}

	coreConfigShutdown(&core->config);

	conPrintf("Core: shutdown complete\n");
}
