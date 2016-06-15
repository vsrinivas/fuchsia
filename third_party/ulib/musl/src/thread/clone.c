#include "pthread_impl.h"
#include <errno.h>

int __clone(int (*func)(void*), void* stack, int flags, void* arg, ...) {
    return -ENOSYS;
}
