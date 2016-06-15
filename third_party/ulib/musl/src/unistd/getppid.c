#include "syscall.h"
#include <unistd.h>

pid_t getppid(void) {
    return __syscall(SYS_getppid);
}
