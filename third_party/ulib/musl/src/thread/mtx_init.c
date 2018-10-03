#include <assert.h>
#include <lib/sync/mtx.h>
#include <threads.h>

static_assert(sizeof(mtx_t) == sizeof(sync_mtx_t), "mtx_t has an unexpected size");

int mtx_init(mtx_t* m, int type) {
    // TODO(kulakowski) Revisit this if anyone actually needs a recursive C11 mutex.
    if (type & mtx_recursive)
        return thrd_error;

    *(sync_mtx_t*)&m->__i = SYNC_MTX_INIT;

    return thrd_success;
}
