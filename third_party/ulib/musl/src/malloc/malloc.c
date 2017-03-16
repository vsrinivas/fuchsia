#define _GNU_SOURCE
#include "atomic.h"
#include "libc.h"
#include "malloc_impl.h"
#include <errno.h>
#include <limits.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <threads.h>

#if defined(__GNUC__) && defined(__PIC__)
#define inline inline __attribute__((always_inline))
#endif

static char* vmo_allocate(size_t len) {
    mx_handle_t vmo;
    if (_mx_vmo_create(len, 0, &vmo) != NO_ERROR) {
        errno = ENOMEM;
        return NULL;
    }

    uintptr_t ptr = 0;
    mx_status_t status =
        _mx_vmar_map(_mx_vmar_root_self(), 0, vmo, 0, len,
                     MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &ptr);
    _mx_handle_close(vmo);

    switch(status) {
    case NO_ERROR:
        break;
    case ERR_ACCESS_DENIED:
        errno = EACCES;
    case ERR_NO_MEMORY:
        errno = ENOMEM;
    case ERR_INVALID_ARGS:
    case ERR_BAD_STATE:
    default:
        errno = EINVAL;
    }

    return (char*)ptr;
}

static int vmo_deallocate(char* start, size_t len) {
    uintptr_t ptr = (uintptr_t)start;
    mx_status_t status = _mx_vmar_unmap(_mx_vmar_root_self(), ptr, len);
    if (status < 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

struct bin {
    mtx_t lock;
    struct chunk* head;
    struct chunk* tail;
};

static struct {
    _Atomic(uint64_t) binmap;
    struct bin bins[64];
    mtx_t free_lock;
} mal;

#define SIZE_ALIGN (4 * sizeof(size_t))
#define SIZE_MASK (-SIZE_ALIGN)
#define MMAP_THRESHOLD (0x1c00 * SIZE_ALIGN)
#define DONTCARE 16
#define RECLAIM 163840

#define FREE_FILL 0x79

#define BIN_TO_CHUNK(i) (MEM_TO_CHUNK(&mal.bins[i].head))

#define ROUND(addr) ((addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

/* Synchronization tools */

static inline void lock_bin(int i) {
    mtx_lock(&mal.bins[i].lock);
    if (!mal.bins[i].head)
        mal.bins[i].head = mal.bins[i].tail = BIN_TO_CHUNK(i);
}

static inline void unlock_bin(int i) {
    mtx_unlock(&mal.bins[i].lock);
}

static int first_set(uint64_t x) {
    return __builtin_ctzll(x);
}

static int bin_index(size_t x) {
    x = x / SIZE_ALIGN - 1;
    if (x <= 32)
        return x;
    if (x > 0x1c00)
        return 63;
    float v = (int)x;
    uint32_t r;
    memcpy(&r, &v, sizeof(v));
    return (r >> 21) - 496;
}

static int bin_index_up(size_t x) {
    x = x / SIZE_ALIGN - 1;
    if (x <= 32)
        return x;
    float v = (int)x;
    uint32_t r;
    memcpy(&r, &v, sizeof(v));
    return ((r + 0x1fffff) >> 21) - 496;
}

#if 0
void __dump_heap(int x)
{
    struct chunk *c;
    int i;
    for (c = (void *)mal.heap; CHUNK_SIZE(c); c = NEXT_CHUNK(c))
        fprintf(stderr, "base %p size %zu (%d) flags %d/%d\n",
            c, CHUNK_SIZE(c), bin_index(CHUNK_SIZE(c)),
            c->csize & 15,
            NEXT_CHUNK(c)->psize & 15);
    for (i=0; i<64; i++) {
        if (mal.bins[i].head != BIN_TO_CHUNK(i) && mal.bins[i].head) {
            fprintf(stderr, "bin %d: %p\n", i, mal.bins[i].head);
            if (!(atomic_load(&mal.binmap) & 1ULL<<i))
                fprintf(stderr, "missing from binmap!\n");
        } else if (atomic_load(&mal.binmap) & 1ULL<<i)
            fprintf(stderr, "binmap wrongly contains %d!\n", i);
    }
}
#endif

static mx_status_t vmo_remap(uintptr_t old_mapping, size_t old_len, size_t new_len, uintptr_t* new_mapping) {
    if (new_len < old_len) {
        // TODO(kulakowski) Partially unmap.
        *new_mapping = old_mapping;
        return NO_ERROR;
    }

    mx_handle_t vmo;
    mx_status_t status = _mx_vmo_create(new_len, 0, &vmo);
    if (status != 0) {
        return status;
    }

    size_t offset = 0;
    uint32_t flags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
    status = _mx_vmar_map(_mx_vmar_root_self(), offset, vmo, 0u, new_len,
                          flags, new_mapping);
    _mx_handle_close(vmo);
    if (status != NO_ERROR) {
        return status;
    }

    memcpy((void*)*new_mapping, (void*)old_mapping, old_len);

    status = _mx_vmar_unmap(_mx_vmar_root_self(), old_mapping, old_len);
    if (status != NO_ERROR) {
        _mx_vmar_unmap(_mx_vmar_root_self(), *new_mapping, new_len);
        return status;
    }

    return NO_ERROR;
}

static void* remap_pages(void* old_addr, size_t old_len, size_t new_len) {
    uintptr_t mapping = (uintptr_t)old_addr;
    if (ROUND(mapping) != mapping) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    if (new_len >= PTRDIFF_MAX) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    uintptr_t new_mapping = 0u;
    mx_status_t status = vmo_remap(mapping, old_len, new_len, &new_mapping);
    if (status != NO_ERROR) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    return (void*)new_mapping;
}

void* __expand_heap(size_t*);

static struct chunk* expand_heap(size_t n) {
    static mtx_t heap_lock;
    static void* end;
    void* p;
    struct chunk* w;

    /* The argument n already accounts for the caller's chunk
     * overhead needs, but if the heap can't be extended in-place,
     * we need room for an extra zero-sized sentinel chunk. */
    n += SIZE_ALIGN;

    mtx_lock(&heap_lock);

    p = __expand_heap(&n);
    if (!p) {
        mtx_unlock(&heap_lock);
        return 0;
    }

    /* If not just expanding existing space, we need to make a
     * new sentinel chunk below the allocated space. */
    if (p != end) {
        /* Valid/safe because of the prologue increment. */
        n -= SIZE_ALIGN;
        p = (char*)p + SIZE_ALIGN;
        w = MEM_TO_CHUNK(p);
        w->psize = 0 | C_INUSE;
    }

    /* Record new heap end and fill in footer. */
    end = (char*)p + n;
    w = MEM_TO_CHUNK(end);
    w->psize = n | C_INUSE;
    w->csize = 0 | C_INUSE;

    /* Fill in header, which may be new or may be replacing a
     * zero-size sentinel header at the old end-of-heap. */
    w = MEM_TO_CHUNK(p);
    w->csize = n | C_INUSE;

    mtx_unlock(&heap_lock);

    return w;
}

static int adjust_size(size_t* n) {
    /* Result of pointer difference must fit in ptrdiff_t. */
    if (*n - 1 > PTRDIFF_MAX - SIZE_ALIGN - PAGE_SIZE) {
        if (*n) {
            errno = ENOMEM;
            return -1;
        } else {
            *n = SIZE_ALIGN;
            return 0;
        }
    }
    *n = (*n + OVERHEAD + SIZE_ALIGN - 1) & SIZE_MASK;
    return 0;
}

static void unbin(struct chunk* c, int i) {
    if (c->prev == c->next)
        atomic_fetch_and(&mal.binmap, ~(1ULL << i));
    c->prev->next = c->next;
    c->next->prev = c->prev;
    c->csize |= C_INUSE;
    NEXT_CHUNK(c)
        ->psize |= C_INUSE;
}

static int alloc_fwd(struct chunk* c) {
    int i;
    size_t k;
    while (!((k = c->csize) & C_INUSE)) {
        i = bin_index(k);
        lock_bin(i);
        if (c->csize == k) {
            unbin(c, i);
            unlock_bin(i);
            return 1;
        }
        unlock_bin(i);
    }
    return 0;
}

static int alloc_rev(struct chunk* c) {
    int i;
    size_t k;
    while (!((k = c->psize) & C_INUSE)) {
        i = bin_index(k);
        lock_bin(i);
        if (c->psize == k) {
            unbin(PREV_CHUNK(c), i);
            unlock_bin(i);
            return 1;
        }
        unlock_bin(i);
    }
    return 0;
}

/* pretrim - trims a chunk _prior_ to removing it from its bin.
 * Must be called with i as the ideal bin for size n, j the bin
 * for the _free_ chunk self, and bin j locked. */
static int pretrim(struct chunk* self, size_t n, int i, int j) {
    size_t n1;
    struct chunk *next, *split;

    /* We cannot pretrim if it would require re-binning. */
    if (j < 40)
        return 0;
    if (j < i + 3) {
        if (j != 63)
            return 0;
        n1 = CHUNK_SIZE(self);
        if (n1 - n <= MMAP_THRESHOLD)
            return 0;
    } else {
        n1 = CHUNK_SIZE(self);
    }
    if (bin_index(n1 - n) != j)
        return 0;

    next = NEXT_CHUNK(self);
    split = (void*)((char*)self + n);

    split->prev = self->prev;
    split->next = self->next;
    split->prev->next = split;
    split->next->prev = split;
    split->psize = n | C_INUSE;
    split->csize = n1 - n;
    next->psize = n1 - n;
    self->csize = n | C_INUSE;
    return 1;
}

static void trim(struct chunk* self, size_t n) {
    size_t n1 = CHUNK_SIZE(self);
    struct chunk *next, *split;

    if (n >= n1 - DONTCARE)
        return;

    next = NEXT_CHUNK(self);
    split = (void*)((char*)self + n);

    split->psize = n | C_INUSE;
    split->csize = n1 - n | C_INUSE;
    next->psize = n1 - n | C_INUSE;
    self->csize = n | C_INUSE;

    free(CHUNK_TO_MEM(split));
}

void* malloc(size_t n) {
    struct chunk* c;
    int i, j;

    if (adjust_size(&n) < 0)
        return 0;

    if (n > MMAP_THRESHOLD) {
        size_t len = n + OVERHEAD + PAGE_SIZE - 1 & -PAGE_SIZE;
        char* base = vmo_allocate(len);
        if (base == NULL)
            return NULL;
        c = (void*)(base + SIZE_ALIGN - OVERHEAD);
        c->csize = len - (SIZE_ALIGN - OVERHEAD);
        c->psize = SIZE_ALIGN - OVERHEAD;
        return CHUNK_TO_MEM(c);
    }

    i = bin_index_up(n);
    for (;;) {
        uint64_t mask = atomic_load(&mal.binmap) & -(1ULL << i);
        if (!mask) {
            c = expand_heap(n);
            if (!c)
                return 0;
            if (alloc_rev(c)) {
                struct chunk* x = c;
                c = PREV_CHUNK(c);
                NEXT_CHUNK(x)
                    ->psize = c->csize = x->csize + CHUNK_SIZE(c);
            }
            break;
        }
        j = first_set(mask);
        lock_bin(j);
        c = mal.bins[j].head;
        if (c != BIN_TO_CHUNK(j)) {
            if (!pretrim(c, n, i, j))
                unbin(c, j);
            unlock_bin(j);
            break;
        }
        unlock_bin(j);
    }

    /* Now patch up in case we over-allocated */
    trim(c, n);

    return CHUNK_TO_MEM(c);
}

void* __malloc0(size_t n) {
    void* p = malloc(n);
    if (p && !IS_MMAPPED(MEM_TO_CHUNK(p))) {
        size_t* z;
        n = (n + sizeof *z - 1) / sizeof *z;
        for (z = p; n; n--, z++)
            if (*z)
                *z = 0;
    }
    return p;
}

void* realloc(void* p, size_t n) {
    struct chunk *self, *next;
    size_t n0, n1;
    void* new;

    if (!p)
        return malloc(n);

    if (adjust_size(&n) < 0)
        return 0;

    self = MEM_TO_CHUNK(p);
    n1 = n0 = CHUNK_SIZE(self);

    if (IS_MMAPPED(self)) {
        size_t extra = self->psize;
        char* base = (char*)self - extra;
        size_t oldlen = n0 + extra;
        size_t newlen = n + extra;
        /* Crash on realloc of freed chunk */
        if (extra & 1)
            __builtin_trap();
        if (newlen < PAGE_SIZE && (new = malloc(n))) {
            memcpy(new, p, n - OVERHEAD);
            free(p);
            return new;
        }
        newlen = (newlen + PAGE_SIZE - 1) & -PAGE_SIZE;
        if (oldlen == newlen)
            return p;
        base = remap_pages(base, oldlen, newlen);
        if (base == MAP_FAILED)
            return newlen < oldlen ? p : 0;
        self = (void*)(base + extra);
        self->csize = newlen - extra;
        return CHUNK_TO_MEM(self);
    }

    next = NEXT_CHUNK(self);

    /* Crash on corrupted footer (likely from buffer overflow) */
    if (next->psize != self->csize)
        __builtin_trap();

    /* Merge adjacent chunks if we need more space. This is not
     * a waste of time even if we fail to get enough space, because our
     * subsequent call to free would otherwise have to do the merge. */
    if (n > n1 && alloc_fwd(next)) {
        n1 += CHUNK_SIZE(next);
        next = NEXT_CHUNK(next);
    }
    /* FIXME: find what's wrong here and reenable it..? */
    if (0 && n > n1 && alloc_rev(self)) {
        self = PREV_CHUNK(self);
        n1 += CHUNK_SIZE(self);
    }
    self->csize = n1 | C_INUSE;
    next->psize = n1 | C_INUSE;

    /* If we got enough space, split off the excess and return */
    if (n <= n1) {
        // memmove(CHUNK_TO_MEM(self), p, n0-OVERHEAD);
        trim(self, n);
        return CHUNK_TO_MEM(self);
    }

    /* As a last resort, allocate a new chunk and copy to it. */
    new = malloc(n - OVERHEAD);
    if (!new)
        return 0;
    memcpy(new, p, n0 - OVERHEAD);
    free(CHUNK_TO_MEM(self));
    return new;
}

void free(void* p) {
    struct chunk* self = MEM_TO_CHUNK(p);
    struct chunk* next;
    size_t final_size, new_size, size;
    int reclaim = 0;
    int i;

    if (!p)
        return;

    if (IS_MMAPPED(self)) {
        size_t extra = self->psize;
        char* base = (char*)self - extra;
        size_t len = CHUNK_SIZE(self) + extra;
        /* Crash on double free */
        if (extra & 1)
            __builtin_trap();
        vmo_deallocate(base, len);
        return;
    }

#if MX_DEBUGLEVEL > 1
    memset(p, FREE_FILL, CHUNK_SIZE(self) - OVERHEAD);
#endif
    final_size = new_size = CHUNK_SIZE(self);
    next = NEXT_CHUNK(self);

    /* Crash on corrupted footer (likely from buffer overflow) */
    if (next->psize != self->csize)
        __builtin_trap();

    for (;;) {
        if (self->psize & next->csize & C_INUSE) {
            self->csize = final_size | C_INUSE;
            next->psize = final_size | C_INUSE;
            i = bin_index(final_size);
            lock_bin(i);
            mtx_lock(&mal.free_lock);
            if (self->psize & next->csize & C_INUSE)
                break;
            mtx_unlock(&mal.free_lock);
            unlock_bin(i);
        }

        if (alloc_rev(self)) {
            self = PREV_CHUNK(self);
            size = CHUNK_SIZE(self);
            final_size += size;
            if (new_size + size > RECLAIM && (new_size + size ^ size) > size)
                reclaim = 1;
        }

        if (alloc_fwd(next)) {
            size = CHUNK_SIZE(next);
            final_size += size;
            if (new_size + size > RECLAIM && (new_size + size ^ size) > size)
                reclaim = 1;
            next = NEXT_CHUNK(next);
        }
    }

    if (!(atomic_load(&mal.binmap) & 1ULL << i))
        atomic_fetch_or(&mal.binmap, 1ULL << i);

    self->csize = final_size;
    next->psize = final_size;
    mtx_unlock(&mal.free_lock);

    self->next = BIN_TO_CHUNK(i);
    self->prev = mal.bins[i].tail;
    self->next->prev = self;
    self->prev->next = self;

    /* Replace middle of large chunks with fresh zero pages */
    if (reclaim) {
        // TODO(kulakowski) Use a magenta-native madvise-alike:
        // uintptr_t a = (uintptr_t)self + SIZE_ALIGN + PAGE_SIZE - 1 & -PAGE_SIZE;
        // uintptr_t b = (uintptr_t)next - SIZE_ALIGN & -PAGE_SIZE;
        // madvise((void*)a, b - a, MADV_DONTNEED);
    }

    unlock_bin(i);
}
