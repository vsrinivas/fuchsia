#include <lib/sync/mtx.h>
#include <threads.h>
#include <zircon/compiler.h>

// Thread safety analysis doesn't extend into the zxr layer, so this
// is marked as no analysis.
int mtx_lock(mtx_t* m) __TA_NO_THREAD_SAFETY_ANALYSIS {
    sync_mtx_lock((sync_mtx_t*)&m->__i);
    return thrd_success;
}
