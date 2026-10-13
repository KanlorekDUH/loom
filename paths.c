/***********************************************************************
 * paths.c — where user data lives
 ***********************************************************************/

#include "paths.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

static char g_dir[512];

const char *DataDir(void)
{
    if (g_dir[0]) return g_dir;

    const char *xdg  = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");

    char base[400];
    if (xdg && *xdg)   snprintf(base, sizeof(base), "%s", xdg);
    else if (home)     snprintf(base, sizeof(base), "%s/.local/share", home);
    else               snprintf(base, sizeof(base), ".");

    /* Best effort — on a normal desktop these already exist. */
    mkdir(base, 0755);

    snprintf(g_dir, sizeof(g_dir), "%s/loom", base);
    mkdir(g_dir, 0755);

    return g_dir;
}

void DataPath(char *buf, size_t n, const char *name)
{
    snprintf(buf, n, "%s/%s", DataDir(), name);
}

void LegacyPath(char *buf, size_t n, const char *name)
{
    snprintf(buf, n, "%s%s", GetApplicationDirectory(), name);
}
