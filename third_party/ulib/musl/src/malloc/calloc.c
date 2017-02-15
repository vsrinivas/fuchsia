#include <errno.h>
#include <stdlib.h>

#include "malloc_impl.h"

void* calloc(size_t m, size_t n) {
    if (n && m > (size_t)-1 / n) {
        errno = ENOMEM;
        return 0;
    }
    return __malloc0(n * m);
}
