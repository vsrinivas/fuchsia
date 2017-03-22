#include "futex_impl.h"
#include "pthread_impl.h"

#include <assert.h>

enum {
    STATE_INIT = 0, // we're the first; run init
    STATE_WAIT = 1, // another thread is running init; wait
    STATE_DONE = 2, // another thread finished running init; just return
    STATE_WAKE = 3, // another thread is running init, waiters present; wait
};

static_assert(STATE_INIT == PTHREAD_ONCE_INIT, "");

static int __pthread_once_full(pthread_once_t* control, void (*init)(void)) {
    for (;;)
        switch (a_cas_shim(control, STATE_INIT, STATE_WAIT)) {
        case STATE_INIT:
            init();

            if (atomic_exchange(control, STATE_DONE) == STATE_WAKE)
                __wake(control, -1);
            return 0;
        case STATE_WAIT:
            /* If this fails, so will __wait. */
            a_cas_shim(control, STATE_WAIT, STATE_WAKE);
        case STATE_WAKE:
            __wait(control, NULL, STATE_WAKE);
            continue;
        case STATE_DONE:
            return 0;
        }
}

int __pthread_once(pthread_once_t* control, void (*init)(void)) {
    /* Return immediately if init finished before, but ensure that
     * effects of the init routine are visible to the caller. */
    if (atomic_load(control) == STATE_DONE) {
        atomic_thread_fence(memory_order_seq_cst);
        return 0;
    }
    return __pthread_once_full(control, init);
}

weak_alias(__pthread_once, pthread_once);
