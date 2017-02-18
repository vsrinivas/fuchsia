#include "pthread_impl.h"

// The compiler supports __builtin_* names that just call these.

void* __get_unsafe_stack_start(void) {
    return __pthread_self()->unsafe_stack.iov_base;
}

void* __get_unsafe_stack_ptr(void) {
    return (void*)__pthread_self()->abi.unsafe_sp;
}
