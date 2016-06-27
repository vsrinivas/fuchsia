#include <runtime/mutex.h>
#include <threads.h>

int mtx_unlock(mtx_t* m) {
    mxr_mutex_unlock((mxr_mutex_t*)&m->__i);
    return thrd_success;
}
