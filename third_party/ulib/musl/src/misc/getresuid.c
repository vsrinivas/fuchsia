#define _GNU_SOURCE
#include "syscall.h"
#include <unistd.h>

int getresuid(uid_t* ruid, uid_t* euid, uid_t* suid) {
    return syscall(SYS_getresuid, ruid, euid, suid);
}
