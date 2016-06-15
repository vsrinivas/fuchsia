#include "syscall.h"
#include <unistd.h>

pid_t getpgrp(void) {
    return __syscall(SYS_getpgid, 0);
}
