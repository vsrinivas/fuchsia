#include "libc.h"
#include <errno.h>
#include <limits.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <stdint.h>

// Size of guard pages (in bytes) to use
#define GUARD_SIZE (1U << 20)

// Size of heap region to use on 64-bit address spaces
#define HEAP_REGION_SIZE (1ULL << 40)

/* Expand the heap in-place via vmar operations, using different strategies
 * for different address space sizes.
 *
 * On 64-bit address spaces, we create a large VMAR for the heap to live in,
 * and allow it to grow contiguously toward higher addresses.
 *
 * On 32-bit address spaces, we create unrelated mappings with an exponential
 * lower bound on growth to make fragmentation asymptotically irrelevant.  We
 * use guard pages here since the heap may be scattered amongst other mappings.
 *
 * The size argument is both an input and an output, since the caller needs
 * to know the size allocated, which will be larger than requested due to page
 * alignment and mmap minimum size rules. The caller is responsible for
 * locking to prevent concurrent calls. */
void* __expand_heap(size_t* pn) {
    static unsigned mmap_step;
    size_t n = *pn;

    if (n > SIZE_MAX / 2 - PAGE_SIZE) {
        goto nomem;
    }
    n += -n & PAGE_SIZE - 1;

    size_t min = (size_t)PAGE_SIZE << mmap_step / 2;
    if (n < min)
        n = min;

    mx_handle_t vmo;
    mx_status_t status = _mx_vmo_create(n, 0, &vmo);
    if (status != NO_ERROR) {
        goto nomem;
    }

    uintptr_t area;
#if _LP64
    static mx_handle_t heap_vmar = MX_HANDLE_INVALID;
    static uintptr_t next_heap_offset = GUARD_SIZE;
    if (heap_vmar == MX_HANDLE_INVALID) {
        status = _mx_vmar_allocate(_mx_vmar_root_self(), 0, HEAP_REGION_SIZE,
                                   MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                                       MX_VM_FLAG_CAN_MAP_SPECIFIC,
                                   &heap_vmar, &area);
        if (status != NO_ERROR) {
            _mx_handle_close(vmo);
            goto nomem;
        }
    }

    status = _mx_vmar_map(heap_vmar, next_heap_offset, vmo, 0, n,
                          MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_SPECIFIC,
                          &area);
    _mx_handle_close(vmo);
    if (status != NO_ERROR) {
        goto nomem;
    }
    next_heap_offset += n;

#else  // _LP64
    /* Allocate a VMAR with space for a GUARD_SIZE guard page on each side */
    mx_handle_t vmar;
    size_t total_alloc = n + 2 * GUARD_SIZE;
    status = _mx_vmar_allocate(_mx_vmar_root_self(), 0, total_alloc,
                               MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE |
                                   MX_VM_FLAG_CAN_MAP_SPECIFIC,
                               &vmar, &area);
    if (status != NO_ERROR) {
        _mx_handle_close(vmo);
        goto nomem;
    }
    status = _mx_vmar_map(vmar, GUARD_SIZE, vmo, 0, n,
                          MX_VM_FLAG_SPECIFIC | MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                          &area);
    _mx_handle_close(vmo);
    if (status != NO_ERROR) {
        _mx_vmar_destroy(vmar);
        _mx_handle_close(vmar);
        goto nomem;
    }
    _mx_handle_close(vmar);
#endif // _LP64

    *pn = n;
    mmap_step++;
    return (void*)area;

nomem:
    errno = ENOMEM;
    return NULL;
}
