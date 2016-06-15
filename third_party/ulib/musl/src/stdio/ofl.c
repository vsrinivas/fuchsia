#include "libc.h"
#include "stdio_impl.h"

#include <runtime/mutex.h>

static FILE* ofl_head;
static mxr_mutex_t ofl_lock;

FILE** __ofl_lock(void) {
    mxr_mutex_lock(&ofl_lock);
    return &ofl_head;
}

void __ofl_unlock(void) {
    mxr_mutex_unlock(&ofl_lock);
}
