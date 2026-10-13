#ifndef PATHS_H
#define PATHS_H

#include <stddef.h>

/* Where the game keeps its user data. Previously these files sat next to the
   executable, inside the project folder — where a build cleanup could (and
   did) delete them. They now live in the per-user data folder:

       $XDG_DATA_HOME/loom   or   ~/.local/share/loom

   The folder is created on first use. */

const char *DataDir(void);
void DataPath(char *buf, size_t n, const char *name);

/* The old beside-the-executable location. Read once, to migrate. */
void LegacyPath(char *buf, size_t n, const char *name);

#endif /* PATHS_H */
