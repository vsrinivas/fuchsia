#define _BSD_SOURCE
#include "libc.h"
#include <stdlib.h>

void* valloc(size_t size) {
    return memalign(PAGE_SIZE, size);
}
