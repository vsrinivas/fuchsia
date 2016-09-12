#include "syscall.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define MAXTRIES 100

char* __randname(char*);

char* tmpnam(char* buf) {
    static char internal[L_tmpnam];
    char s[] = "/tmp/tmpnam_XXXXXX";
    int try
        ;
    int r;
    for (try = 0; try < MAXTRIES; try ++) {
        __randname(s + 12);
        r = lstat(s, &(struct stat){0});
        if (r == 0)
            return strcpy(buf ? buf : internal, s);
    }
    return 0;
}
