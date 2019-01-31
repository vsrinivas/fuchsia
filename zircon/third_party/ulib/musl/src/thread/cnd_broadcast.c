#include <threads.h>

#include "threads_impl.h"

int cnd_broadcast(cnd_t* c) {
    __private_cond_signal(c, -1);
    return thrd_success;
}
