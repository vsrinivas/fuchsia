#include "threads_impl.h"

// C11 does not define any way for applications to know the maximum
// number of tss_t slots. pthreads, however, does, via the
// PTHREAD_KEYS_MAX constant. So we allow that bit of pthreads to
// bleed over here (and into sysconf, which also reports the value)
// rather than go through some circuituous pattern to define an
// internal constant that's just the same as the pthread one.

typedef void (*key_t)(void*);
static _Atomic(key_t) keys[PTHREAD_KEYS_MAX];

static void nodtor(void* dummy) {}

int tss_create(tss_t* k, void (*dtor)(void*)) {
    unsigned i = (uintptr_t)&k / 16 % PTHREAD_KEYS_MAX;
    unsigned j = i;

    if (!dtor)
        dtor = nodtor;
    do {
        key_t expected = NULL;
        if (atomic_compare_exchange_strong(&keys[j], &expected, dtor)) {
            *k = j;
            return 0;
        }
    } while ((j = (j + 1) % PTHREAD_KEYS_MAX) != i);
    return EAGAIN;
}

void tss_delete(tss_t k) {
    atomic_store(&keys[k], NULL);
}

void __thread_tsd_run_dtors(void) {
    thrd_t self = __thrd_current();
    int i, j, not_finished = self->tsd_used;
    for (j = 0; not_finished && j < TSS_DTOR_ITERATIONS; j++) {
        not_finished = 0;
        for (i = 0; i < PTHREAD_KEYS_MAX; i++) {
            if (self->tsd[i] && atomic_load(&keys[i])) {
                void* tmp = self->tsd[i];
                self->tsd[i] = 0;
                atomic_load(&keys[i])(tmp);
                not_finished = 1;
            }
        }
    }
}
