#include "syscall.h"
#include <sys/stat.h>

mode_t umask(mode_t mode) {
    return syscall(SYS_umask, mode);
}
