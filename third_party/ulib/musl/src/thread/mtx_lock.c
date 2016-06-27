#include <runtime/mutex.h>
#include <threads.h>

int mtx_lock(mtx_t* m) {
    mxr_mutex_lock((mxr_mutex_t*)&m->__i);
    return thrd_success;
}
