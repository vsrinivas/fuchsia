#include "syscall.h"
#include <unistd.h>

void sync(void) {
    __syscall(SYS_sync);
}
