#include "syscall.h"
#include <unistd.h>

int getgroups(int count, gid_t list[]) {
    return syscall(SYS_getgroups, count, list);
}
