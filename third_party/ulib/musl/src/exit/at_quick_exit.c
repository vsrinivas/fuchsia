#include "libc.h"
#include <stdlib.h>

#include <runtime/mutex.h>

#define COUNT 32

static void (*funcs[COUNT])(void);
static int count;
static mxr_mutex_t lock;

void __funcs_on_quick_exit(void) {
    void (*func)(void);
    mxr_mutex_lock(&lock);
    while (count > 0) {
        func = funcs[--count];
        mxr_mutex_unlock(&lock);
        func();
        mxr_mutex_lock(&lock);
    }
}

int at_quick_exit(void (*func)(void)) {
    if (count == 32) return -1;
    mxr_mutex_lock(&lock);
    funcs[count++] = func;
    mxr_mutex_unlock(&lock);
    return 0;
}
