#define _GNU_SOURCE
#include "syscall.h"
#include <unistd.h>

int setdomainname(const char* name, size_t len) {
    return syscall(SYS_setdomainname, name, len);
}
