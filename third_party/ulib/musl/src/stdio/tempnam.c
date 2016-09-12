#include "syscall.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define MAXTRIES 100

char* __randname(char*);

char* tempnam(const char* dir, const char* pfx) {
    char s[PATH_MAX];
    size_t l, dl, pl;
    int try
        ;
    int r;

    if (!dir)
        dir = P_tmpdir;
    if (!pfx)
        pfx = "temp";

    dl = strlen(dir);
    pl = strlen(pfx);
    l = dl + 1 + pl + 1 + 6;

    if (l >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return 0;
    }

    memcpy(s, dir, dl);
    s[dl] = '/';
    memcpy(s + dl + 1, pfx, pl);
    s[dl + 1 + pl] = '_';
    s[l] = 0;

    for (try = 0; try < MAXTRIES; try ++) {
        __randname(s + l - 6);
        r = lstat(s, &(struct stat){0});
        if (r == 0)
            return strdup(s);
    }
    return 0;
}
