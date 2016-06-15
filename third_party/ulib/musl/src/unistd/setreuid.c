#include "libc.h"
#include "syscall.h"
#include <unistd.h>

int setreuid(uid_t ruid, uid_t euid) {
    return __setxid(SYS_setreuid, ruid, euid, 0);
}
