#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <sys/stat.h>

#include "sys_main.h"
#include "config.h"

static int fileExists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static const char *findLaunchScript(const char *exeDir)
{
	static char path[PATH_MAX];

	/* 1. Check argv[1] - handled by caller if provided */

	/* 2. Check <exe_dir>/Launch.lua */
	snprintf(path, sizeof(path), "%s/Launch.lua", exeDir);
	if (fileExists(path))
		return path;

	/* 3. Check <exe_dir>/../src/Launch.lua (dev layout) */
	snprintf(path, sizeof(path), "%s/../src/Launch.lua", exeDir);
	if (fileExists(path))
		return path;

	/* 4. Check <exe_dir>/src/Launch.lua */
	snprintf(path, sizeof(path), "%s/src/Launch.lua", exeDir);
	if (fileExists(path))
		return path;

	/* 5. Check cwd/Launch.lua */
	if (fileExists("Launch.lua"))
		return "Launch.lua";

	/* 6. Check cwd/../src/Launch.lua */
	if (fileExists("../src/Launch.lua"))
		return "../src/Launch.lua";

	return NULL;
}

int main(int argc, char** argv)
{
    fprintf(stderr, CFG_VERSION " Linux, built " __DATE__ "\n");

    char exeDir[PATH_MAX];
    char *shiftArgv[64];
    int shiftArgc;
    char **shiftArgvPtr;
    static char scriptPath[PATH_MAX];

    /* Find directory of this executable */
    {
        char buf[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len != -1) {
            buf[len] = '\0';
            strncpy(exeDir, dirname(buf), sizeof(exeDir) - 1);
            exeDir[sizeof(exeDir) - 1] = '\0';
        } else {
            exeDir[0] = '\0';
        }
    }

    /* Check if user passed a script as argv[1] */
    if (argc > 1) {
        const char *ext = strrchr(argv[1], '.');
        if (ext && (strcmp(ext, ".lua") == 0 || strcmp(ext, ".cfg") == 0)) {
            shiftArgc = argc - 1;
            shiftArgv[0] = argv[1];
            for (int i = 2; i < argc && i < 64; i++)
                shiftArgv[i - 1] = argv[i];
            shiftArgv[shiftArgc] = NULL;
            shiftArgvPtr = shiftArgv;
            goto run;
        }
    }

    /* Auto-find Launch.lua */
    {
        const char *script = findLaunchScript(exeDir);
        if (!script) {
            fprintf(stderr, "Error: Could not find Launch.lua\n");
            fprintf(stderr, "Searched:\n");
            fprintf(stderr, "  %s/Launch.lua\n", exeDir);
            fprintf(stderr, "  %s/../src/Launch.lua\n", exeDir);
            fprintf(stderr, "  %s/src/Launch.lua\n", exeDir);
            fprintf(stderr, "  ./Launch.lua\n");
            fprintf(stderr, "  ../src/Launch.lua\n");
            return 1;
        }
        strncpy(scriptPath, script, sizeof(scriptPath) - 1);
        scriptPath[sizeof(scriptPath) - 1] = '\0';
    }

    /* argv[0] = script, argv[1..] = remaining args */
    shiftArgv[0] = scriptPath;
    shiftArgc = 1;
    for (int i = 1; i < argc && i < 63; i++)
        shiftArgv[i] = argv[i];
    shiftArgc = argc;
    shiftArgv[shiftArgc] = NULL;
    shiftArgvPtr = shiftArgv;

run:
    sysInit(shiftArgc, shiftArgvPtr);
    int running = 1;
    while (running) {
        running = sysRun(shiftArgc, shiftArgvPtr);
    }
    sysShutdown();
    fprintf(stderr, "Exiting.\n");
    return 0;
}
