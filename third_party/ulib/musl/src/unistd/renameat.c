#include "syscall.h"
#include <stdio.h>

int renameat(int oldfd, const char* old, int newfd, const char* new) {
    return syscall(SYS_renameat, oldfd, old, newfd, new);
}
