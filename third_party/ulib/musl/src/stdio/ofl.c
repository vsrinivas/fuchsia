#include "libc.h"
#include "stdio_impl.h"

#include <zircon/compiler.h>
#include <threads.h>

static FILE* ofl_head;
static mtx_t ofl_lock;

FILE** __ofl_lock(void) __TA_ACQUIRE(ofl_lock) {
    mtx_lock(&ofl_lock);
    return &ofl_head;
}

void __ofl_unlock(void) __TA_RELEASE(ofl_lock) {
    mtx_unlock(&ofl_lock);
}
