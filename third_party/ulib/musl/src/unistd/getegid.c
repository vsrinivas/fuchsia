#include "syscall.h"
#include <unistd.h>

gid_t getegid(void) {
    return __syscall(SYS_getegid);
}
