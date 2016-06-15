#include "libc.h"
#include "syscall.h"
#include <unistd.h>

int setegid(gid_t egid) {
    return __setxid(SYS_setresgid, -1, egid, -1);
}
