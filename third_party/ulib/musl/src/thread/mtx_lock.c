#include <runtime/mutex.h>
#include <threads.h>

int mtx_lock(mtx_t* m) {
    zxr_mutex_lock((zxr_mutex_t*)&m->__i);
    return thrd_success;
}
