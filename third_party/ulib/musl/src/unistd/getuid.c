#include "syscall.h"
#include <unistd.h>

uid_t getuid(void) {
    return __syscall(SYS_getuid);
}
