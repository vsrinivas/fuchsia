#include <threads.h>

#include "pthread_impl.h"

_Noreturn void thrd_exit(int result) {
    pthread_exit((void*)(intptr_t)result);
}
