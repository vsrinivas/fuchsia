#include "libc.h"
#include "syscall.h"
#include <unistd.h>

int seteuid(uid_t euid) {
    return __setxid(SYS_setresuid, -1, euid, -1);
}
