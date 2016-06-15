#include "syscall.h"
#include <errno.h>

int brk(void* end) {
    return __syscall_ret(-ENOMEM);
}
