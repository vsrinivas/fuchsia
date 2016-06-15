#define _GNU_SOURCE
#include "libc.h"
#include "syscall.h"
#include <unistd.h>

int setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
    return __setxid(SYS_setresgid, rgid, egid, sgid);
}
