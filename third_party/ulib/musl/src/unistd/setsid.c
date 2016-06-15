#include "syscall.h"
#include <unistd.h>

pid_t setsid(void) {
    return syscall(SYS_setsid);
}
