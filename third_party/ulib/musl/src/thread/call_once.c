#include <threads.h>

#include "pthread_impl.h"

void call_once(once_flag* flag, void (*func)(void)) {
    __pthread_once((pthread_once_t*)flag, func);
}
