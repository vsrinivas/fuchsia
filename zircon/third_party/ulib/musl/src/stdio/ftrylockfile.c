#include "threads_impl.h"
#include "stdio_impl.h"
#include <limits.h>

int ftrylockfile(FILE* f) {
    int tid = __thread_get_tid();
    if (f->lock == tid) {
        if (f->lockcount == LONG_MAX)
            return -1;
        f->lockcount++;
        return 0;
    }
    if (atomic_load(&f->lock) < 0)
        atomic_store(&f->lock, 0);
    if (atomic_load(&f->lock) || a_cas_shim(&f->lock, 0, tid))
        return -1;
    f->lockcount = 1;
    return 0;
}
