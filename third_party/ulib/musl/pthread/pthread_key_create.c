#include "threads_impl.h"

typedef void (*key_t)(void*);
static _Atomic(key_t) keys[PTHREAD_KEYS_MAX];

static void nodtor(void* dummy) {}

int __pthread_key_create(pthread_key_t* k, void (*dtor)(void*)) {
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

int __pthread_key_delete(pthread_key_t k) {
    atomic_store(&keys[k], NULL);
    return 0;
}

void __pthread_tsd_run_dtors(void) {
    pthread_t self = __pthread_self();
    int i, j, not_finished = self->tsd_used;
    for (j = 0; not_finished && j < PTHREAD_DESTRUCTOR_ITERATIONS; j++) {
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

weak_alias(__pthread_key_delete, pthread_key_delete);
weak_alias(__pthread_key_create, pthread_key_create);
