#include "syscall.h"
#include <unistd.h>

pid_t getpgid(pid_t pid) {
    return syscall(SYS_getpgid, pid);
}
