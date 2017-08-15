#include "pthread_impl.h"

// The compiler supports __builtin_* names that just call these.

void* __get_unsafe_stack_start(void) {
    return __thrd_current()->unsafe_stack.iov_base;
}

void* __get_unsafe_stack_ptr(void) {
    return (void*)__thrd_current()->abi.unsafe_sp;
}
