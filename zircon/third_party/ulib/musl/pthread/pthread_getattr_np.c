#define _GNU_SOURCE
#include <sys/mman.h>

#include "libc.h"
#include "threads_impl.h"

int pthread_getattr_np(pthread_t t, pthread_attr_t* a) {
  *a = (pthread_attr_t){};
  a->_a_detach = zxr_thread_detached(&t->zxr_thread);
  a->_a_stackaddr = t->safe_stack.iov_base;
  a->_a_stacksize = t->safe_stack.iov_len;
  return 0;
}
