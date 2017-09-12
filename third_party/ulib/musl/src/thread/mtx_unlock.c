#include <runtime/mutex.h>
#include <threads.h>

int mtx_unlock(mtx_t* m) {
    zxr_mutex_unlock((zxr_mutex_t*)&m->__i);
    return thrd_success;
}
