#ifndef SG_CORE_CONFIG_H
#define SG_CORE_CONFIG_H

#include "common.h"

#define CFG_NONE    0
#define CFG_ARCHIVE (1 << 0)

typedef struct cfgVar_s {
	char  name[64];
	char *stringVal;
	int   flags;
	struct cfgVar_s *next;
} cfgVar_t;

typedef struct coreConfig_s {
	cfgVar_t *vars;
} coreConfig_t;

void         coreConfigInit(coreConfig_t *cfg);
void         coreConfigShutdown(coreConfig_t *cfg);
int          coreConfigLoad(coreConfig_t *cfg, const char *path);
int          coreConfigSave(coreConfig_t *cfg, const char *path);
cfgVar_t    *coreConfigFind(coreConfig_t *cfg, const char *name);
void         coreConfigSetString(coreConfig_t *cfg, const char *name, const char *value);
int          coreConfigGetInt(coreConfig_t *cfg, const char *name, int def);
float        coreConfigGetFloat(coreConfig_t *cfg, const char *name, float def);
int          coreConfigGetBool(coreConfig_t *cfg, const char *name, int def);

#endif
