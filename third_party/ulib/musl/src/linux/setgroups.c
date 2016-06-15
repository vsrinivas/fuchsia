#define _GNU_SOURCE
#include "syscall.h"
#include <unistd.h>

int setgroups(size_t count, const gid_t list[]) {
    return syscall(SYS_setgroups, count, list);
}
