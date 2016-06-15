#include "syscall.h"
#include <sys/resource.h>

int setpriority(int which, id_t who, int prio) {
    return syscall(SYS_setpriority, which, who, prio);
}
