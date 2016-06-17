#include "libc.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include <runtime/mutex.h>

#define ALIGN 16

void* __expand_heap(size_t*);

static void* __simple_malloc(size_t n) {
    static char *cur, *end;
    static mxr_mutex_t lock;
    size_t align = 1, pad;
    void* p;

    if (!n)
        n++;
    while (align < n && align < ALIGN)
        align += align;

    mxr_mutex_lock(&lock);

    pad = -(uintptr_t)cur & align - 1;

    if (n <= SIZE_MAX / 2 + ALIGN)
        n += pad;

    if (n > end - cur) {
        size_t m = n;
        char* new = __expand_heap(&m);
        if (!new) {
            mxr_mutex_unlock(&lock);
            return 0;
        }
        if (new != end) {
            cur = new;
            n -= pad;
            pad = 0;
        }
        end = new + m;
    }

    p = cur + pad;
    cur += n;
    mxr_mutex_unlock(&lock);
    return p;
}

weak_alias(__simple_malloc, malloc);
weak_alias(__simple_malloc, __malloc0);
