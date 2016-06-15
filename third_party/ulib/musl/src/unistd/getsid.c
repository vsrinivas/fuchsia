#include "syscall.h"
#include <unistd.h>

pid_t getsid(pid_t pid) {
    return syscall(SYS_getsid, pid);
}
