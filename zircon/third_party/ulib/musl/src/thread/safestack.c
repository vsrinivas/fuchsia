#include "threads_impl.h"

// The compiler supports __builtin_* names that just call these.

void* __get_unsafe_stack_start(void) { return __thrd_current()->unsafe_stack.iov_base; }

void* __get_unsafe_stack_top(void) {
  const struct iovec* stack = &__thrd_current()->unsafe_stack;
  return stack->iov_base + stack->iov_len;
}

void* __get_unsafe_stack_ptr(void) { return (void*)__thrd_current()->abi.unsafe_sp; }
