#include <lib/sync/mutex.h>
#include <threads.h>

int mtx_trylock(mtx_t* m) {
    zx_status_t status = sync_mutex_trylock((sync_mutex_t*)&m->__i);
    switch (status) {
    default:
        return thrd_error;
    case 0:
        return thrd_success;
    case ZX_ERR_BAD_STATE:
        return thrd_busy;
    }
}
