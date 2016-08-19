#include "libc.h"
#include "stdio_impl.h"

#include <threads.h>

static FILE* ofl_head;
static mtx_t ofl_lock;

FILE** __ofl_lock(void) {
    mtx_lock(&ofl_lock);
    return &ofl_head;
}

void __ofl_unlock(void) {
    mtx_unlock(&ofl_lock);
}
