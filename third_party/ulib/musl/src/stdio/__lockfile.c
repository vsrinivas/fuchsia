#include "futex_impl.h"
#include "threads_impl.h"
#include "stdio_impl.h"

int __lockfile(FILE* f) {
    int owner, tid = __thread_get_tid();
    if (atomic_load(&f->lock) == tid)
        return 0;
    while ((owner = a_cas_shim(&f->lock, 0, tid)))
        __wait(&f->lock, &f->waiters, owner);
    return 1;
}

void __unlockfile(FILE* f) {
    atomic_store(&f->lock, 0);

    /* The following read is technically invalid under situations
     * of self-synchronized destruction. Another thread may have
     * called fclose as soon as the above store has completed.
     * Nonetheless, since FILE objects always live in memory
     * obtained by malloc from the heap, it's safe to assume
     * the dereferences below will not fault. In the worst case,
     * a spurious syscall will be made. If the implementation of
     * malloc changes, this assumption needs revisiting. */

    if (atomic_load(&f->waiters))
        __wake(&f->lock, 1);
}
