#include "syscall.h"
#include <unistd.h>

gid_t getgid(void) {
    return __syscall(SYS_getgid);
}
