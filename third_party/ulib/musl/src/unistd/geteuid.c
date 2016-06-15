#include "syscall.h"
#include <unistd.h>

uid_t geteuid(void) {
    return __syscall(SYS_geteuid);
}
