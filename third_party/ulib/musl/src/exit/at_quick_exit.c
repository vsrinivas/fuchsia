#define _ALL_SOURCE
#include "libc.h"
#include <stdlib.h>
#include <threads.h>

#define COUNT 32

static void (*funcs[COUNT])(void);
static int count;
static mtx_t lock = MTX_INIT;

void __funcs_on_quick_exit(void) {
    void (*func)(void);
    mtx_lock(&lock);
    while (count > 0) {
        func = funcs[--count];
        mtx_unlock(&lock);
        func();
        mtx_lock(&lock);
    }
}

int at_quick_exit(void (*func)(void)) {
    if (count == 32)
        return -1;
    mtx_lock(&lock);
    funcs[count++] = func;
    mtx_unlock(&lock);
    return 0;
}
