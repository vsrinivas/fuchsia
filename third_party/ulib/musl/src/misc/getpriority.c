#include "syscall.h"
#include <sys/resource.h>

int getpriority(int which, id_t who) {
    int ret = syscall(SYS_getpriority, which, who);
    if (ret < 0) return ret;
    return 20 - ret;
}
