#include <runtime/mutex.h>
#include <threads.h>

int mtx_trylock(mtx_t* m) {
    mx_status_t status = mxr_mutex_trylock((mxr_mutex_t*)&m->__i);
    switch (status) {
    default:
        return thrd_error;
    case 0:
        return thrd_success;
    case MX_ERR_BAD_STATE:
        return thrd_busy;
    }
}
