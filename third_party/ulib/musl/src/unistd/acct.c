#define _GNU_SOURCE
#include "libc.h"
#include "syscall.h"
#include <unistd.h>

int acct(const char* filename) {
    return syscall(SYS_acct, filename);
}
