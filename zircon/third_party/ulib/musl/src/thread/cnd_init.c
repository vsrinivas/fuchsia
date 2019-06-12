#include <lib/sync/mutex.h>
#include <threads.h>

int cnd_init(cnd_t* c) {
    *c = (cnd_t){};
    *((sync_mutex_t*)(&c->_c_lock)) = SYNC_MUTEX_INIT;
    return thrd_success;
}
