#define _GNU_SOURCE
#include "libc.h"
#include "pthread_impl.h"
#include <sys/mman.h>

int pthread_getattr_np(pthread_t t, pthread_attr_t* a) {
    *a = (pthread_attr_t){0};
    a->_a_detach = !!t->detached;
    if (t->stack) {
        a->_a_stackaddr = (uintptr_t)t->stack;
        a->_a_stacksize = t->stack_size - DEFAULT_STACK_SIZE;
    } else {
        a->_a_stackaddr = libc.stack_base;
        a->_a_stacksize = libc.stack_size - DEFAULT_STACK_SIZE;
    }
    return 0;
}
