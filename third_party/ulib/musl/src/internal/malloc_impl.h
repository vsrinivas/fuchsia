#pragma once

#include <stddef.h>

#define HEAP_DEBUG 1

struct chunk {
    // psize is the size of the previous chunk, csize of this one
    // size is the distance (in bytes) from this chunk header to the next one.
    size_t psize, csize;
    // These pointers only exist in freed chunks
    struct chunk *next, *prev;
};

// psize and csize
#define OVERHEAD (2 * sizeof(size_t))
#define MEM_SIZE_FROM_CHUNK(c) (CHUNK_SIZE(c) - OVERHEAD)
#define CHUNK_SIZE(c) ((c)->csize & -2)
#define CHUNK_PSIZE(c) ((c)->psize & -2)
#define PREV_CHUNK(c) ((struct chunk*)((char*)(c)-CHUNK_PSIZE(c)))
#define NEXT_CHUNK(c) ((struct chunk*)((char*)(c) + CHUNK_SIZE(c)))
#define MEM_TO_CHUNK(p) ((struct chunk*)((char*)(p)-OVERHEAD))
#define CHUNK_TO_MEM(c) (void*)((char*)(c) + OVERHEAD)

#define C_INUSE ((size_t)1)
#define IS_MMAPPED(c) !((c)->csize & (C_INUSE))

void __donate_heap(void* start, void* end)
    __attribute__((visibility("hidden")));
