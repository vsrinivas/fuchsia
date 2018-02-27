#include <threads.h>

#include "threads_impl.h"

void tss_delete(tss_t key) {
    __pthread_key_delete(key);
}
