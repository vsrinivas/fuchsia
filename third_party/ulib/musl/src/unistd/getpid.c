#include "syscall.h"
#include <unistd.h>

pid_t getpid(void) {
    return __syscall(SYS_getpid);
}
