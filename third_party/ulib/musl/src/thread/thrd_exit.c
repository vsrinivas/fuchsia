#include <threads.h>

#include "threads_impl.h"

_Noreturn void thrd_exit(int result) {
    __pthread_exit((void*)(intptr_t)result);
}
