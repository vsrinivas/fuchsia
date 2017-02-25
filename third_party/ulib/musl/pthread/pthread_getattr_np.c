#define _GNU_SOURCE
#include "libc.h"
#include "pthread_impl.h"
#include <sys/mman.h>

int pthread_getattr_np(pthread_t t, pthread_attr_t* a) {
    *a = (pthread_attr_t){};
    a->_a_detach = mxr_thread_detached(&t->mxr_thread);
    a->_a_stackaddr = t->safe_stack.iov_base;
    a->_a_stacksize = t->safe_stack.iov_len;
    return 0;
}
