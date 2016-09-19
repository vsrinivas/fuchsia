#include "libc.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/mman.h>

void* __mmap(void*, size_t, int, int, int, off_t);

/* Expand the heap in-place if brk can be used, or otherwise via mmap,
 * using an exponential lower bound on growth by mmap to make
 * fragmentation asymptotically irrelevant. The size argument is both
 * an input and an output, since the caller needs to know the size
 * allocated, which will be larger than requested due to page alignment
 * and mmap minimum size rules. The caller is responsible for locking
 * to prevent concurrent calls. */

void* __expand_heap(size_t* pn) {
    static unsigned mmap_step;
    size_t n = *pn;
// TODO(teisenbe): Remove this and the use of MAP_FIXED below when it's
// time to do ASLR.  This is present for now to move the heap away from
// other allocations
#if _LP64
    // Start the heap at 1TB if we're 64-bit
    static void* next_base = (void*)(1ULL << 40);
#else
    // Start the heap at 512MB if we're 32-bit
    static void* next_base = (void*)(1ULL << 29);
#endif

    if (n > SIZE_MAX / 2 - PAGE_SIZE) {
        errno = ENOMEM;
        return 0;
    }
    n += -n & PAGE_SIZE - 1;

    size_t min = (size_t)PAGE_SIZE << mmap_step / 2;
    if (n < min)
        n = min;
    void* area = __mmap(next_base, n, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (area == MAP_FAILED)
        return 0;
    *pn = n;
    next_base = area + n;
    mmap_step++;

    return area;
}
