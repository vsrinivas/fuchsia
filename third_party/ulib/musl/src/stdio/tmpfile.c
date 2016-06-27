#include <fcntl.h>
#include <stdio.h>

#include "stdio_impl.h"
#include "syscall.h"

#define MAXTRIES 100

char* __randname(char*);

FILE* tmpfile(void) {
    char s[] = "/tmp/tmpfile_XXXXXX";
    int fd;
    FILE* f;
    int try
        ;
    for (try = 0; try < MAXTRIES; try ++) {
        __randname(s + 13);
        fd = sys_open(s, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            unlink(s);
            f = __fdopen(fd, "w+");
            if (!f)
                __syscall(SYS_close, fd);
            return f;
        }
    }
    return 0;
}
