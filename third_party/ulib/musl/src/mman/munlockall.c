#include "syscall.h"
#include <sys/mman.h>

int munlockall(void) {
    return syscall(SYS_munlockall);
}
