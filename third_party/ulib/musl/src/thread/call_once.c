#include <threads.h>

#include <assert.h>

#include "futex_impl.h"

enum {
    STATE_INIT = 0, // we're the first; run init
    STATE_WAIT = 1, // another thread is running init; wait
    STATE_DONE = 2, // another thread finished running init; just return
    STATE_WAKE = 3, // another thread is running init, waiters present; wait
};

static_assert(STATE_INIT == ONCE_FLAG_INIT, "");

static void once_full(once_flag* control, void (*init)(void)) {
    for (;;)
        switch (a_cas_shim(control, STATE_INIT, STATE_WAIT)) {
        case STATE_INIT:
            init();

            if (atomic_exchange(control, STATE_DONE) == STATE_WAKE)
                __wake(control, -1);
            return;
        case STATE_WAIT:
            /* If this fails, so will __wait. */
            a_cas_shim(control, STATE_WAIT, STATE_WAKE);
        case STATE_WAKE:
            __wait(control, NULL, STATE_WAKE);
            continue;
        case STATE_DONE:
            return;
        }
}

void call_once(once_flag* control, void (*init)(void)) {
    /* Return immediately if init finished before, but ensure that
     * effects of the init routine are visible to the caller. */
    if (atomic_load(control) == STATE_DONE) {
        atomic_thread_fence(memory_order_seq_cst);
        return;
    }
    once_full(control, init);
}
