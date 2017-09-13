#include <runtime/mutex.h>
#include <threads.h>
#include <zircon/compiler.h>

// Thread safety analysis doesn't extend into the zxr layer, so this
// is marked as no analysis.
int mtx_unlock(mtx_t* m) __TA_NO_THREAD_SAFETY_ANALYSIS {
    zxr_mutex_unlock((zxr_mutex_t*)&m->__i);
    return thrd_success;
}
