#define _GNU_SOURCE
#include "libc.h"
#include "syscall.h"
#include <unistd.h>

int setresuid(uid_t ruid, uid_t euid, uid_t suid) {
    return __setxid(SYS_setresuid, ruid, euid, suid);
}
