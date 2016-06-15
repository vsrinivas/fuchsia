#define _GNU_SOURCE
#include "syscall.h"
#include <unistd.h>

int sethostname(const char* name, size_t len) {
    return syscall(SYS_sethostname, name, len);
}
