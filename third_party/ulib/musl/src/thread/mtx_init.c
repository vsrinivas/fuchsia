#include <assert.h>
#include <runtime/mutex.h>
#include <threads.h>

static_assert(sizeof(mtx_t) == sizeof(zxr_mutex_t), "mtx_t has an unexpected size");

int mtx_init(mtx_t* m, int type) {
    // TODO(kulakowski) Revisit this if anyone actually needs a recursive C11 mutex.
    if (type & mtx_recursive)
        return thrd_error;

    *(zxr_mutex_t*)&m->__i = ZXR_MUTEX_INIT;

    return thrd_success;
}
