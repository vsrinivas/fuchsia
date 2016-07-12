// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <assert.h>
#include <ddk/io-alloc.h>
#include <magenta/syscalls.h>
#include <runtime/mutex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_ALIGN 8

// minimum total block size
#define MIN_BLOCK_SIZE 64

typedef struct io_block_header io_block_header_t;

struct io_alloc {
    mx_paddr_t phys;
    void* virt;
    size_t size;
    intptr_t virt_offset;
    mxr_mutex_t mutex;
    io_block_header_t* free_list;
};

struct io_block_header {
    // Allocated: pointer to beginning of block
    // Free: pointer to next free block
    io_block_header_t* ptr;
    // total size of the block, including header and alignment padding
    size_t size;
};

io_alloc_t* io_alloc_init(size_t size) {
    io_alloc_t* ioa = calloc(1, sizeof(io_alloc_t));
    if (!ioa)
        return NULL;

    ioa->mutex = MXR_MUTEX_INIT;

    mx_paddr_t phys;
    void* virt;
    mx_status_t status = mx_alloc_device_memory(size, &phys, &virt);
    if (status) {
        printf("mx_alloc_device_memory failed %d\n", status);
        free(ioa);
        return NULL;
    }

    ioa->phys = phys;
    ioa->virt = virt;
    ioa->size = size;
    ioa->virt_offset = (uintptr_t)virt - phys;

    io_block_header_t* free_list = virt;
    free_list->size = size;
    free_list->ptr = NULL;
    ioa->free_list = free_list;

    return ioa;
}

void io_alloc_free(io_alloc_t* ioa) {
    // FIXME (voydanoff) no way to release memory allocated via mx_alloc_device_memory
    free(ioa);
}

void* io_malloc(io_alloc_t* ioa, size_t size) {
    return io_memalign(ioa, MIN_ALIGN, size);
}

void* io_calloc(io_alloc_t* ioa, size_t count, size_t size) {
    size_t len = count * size;
    void* result = io_memalign(ioa, MIN_ALIGN, len);
    if (!result)
        return NULL;
    memset(result, 0, len);
    return result;
}

void* io_memalign(io_alloc_t* ioa, size_t align, size_t size) {
    void* result = NULL;

    // align must be power of 2
    if ((align & -align) != align) {
        printf("bad alignment %zu for io_memalign\n", align);
        return NULL;
    }
    if (align < MIN_ALIGN)
        align = MIN_ALIGN;

    mxr_mutex_lock(&ioa->mutex);

    io_block_header_t* block = ioa->free_list;
    io_block_header_t* prev = NULL;

    while (block) {
        size_t block_size = block->size;

        // compute aligned address past block header
        uintptr_t ptr = (uintptr_t)block + sizeof(io_block_header_t);
        ptr = (ptr + align - 1) & -align;
        uintptr_t block_end = (uintptr_t)block + block_size;
        size_t aligned_block_size = block_end - ptr;

        if (aligned_block_size >= size) {
            // pull the block from the free list
            if (prev) {
                prev->ptr = block->ptr;
            } else {
                ioa->free_list = block->ptr;
            }

            // new block header will be immediately before *ptr
            io_block_header_t* header = &((io_block_header_t*)ptr)[-1];

            // set ptr to point to beginning of block
            header->ptr = block;

            if (aligned_block_size - size >= MIN_BLOCK_SIZE) {
                // split our free block

                // next_block is pointer to remainder of the free block
                uintptr_t next_block = ptr + size;
                next_block = (next_block + MIN_ALIGN - 1) & -MIN_ALIGN;

                // newly allocated block size is next_block minus beginning of our free block
                header->size = next_block - (uintptr_t)block;

                io_block_header_t* next_header = (io_block_header_t*)next_block;
                next_header->size = block_end - next_block;

                // add remainder of block to free list
                next_header->ptr = ioa->free_list;
                ioa->free_list = next_header;
            } else {
                header->size = block_size;
            }

            result = (void*)ptr;
            break;
        }

        prev = block;
        block = block->ptr;
    }

    mxr_mutex_unlock(&ioa->mutex);

    if (!result)
        printf("OUT OF MEMORY!!!\n");
    return result;
}

void io_free(io_alloc_t* ioa, void* ptr) {
    if (!ptr)
        return;

    assert(ptr > ioa->virt && ptr < ioa->virt + ioa->size);

    // block header is immediately before *ptr
    io_block_header_t* header = &((io_block_header_t*)ptr)[-1];
    size_t size = header->size;

    // back up to beginning of block (might have been padded for alignment)
    header = header->ptr;
    header->size = size;

    mxr_mutex_lock(&ioa->mutex);

    // add to free list
    // TODO (voydanoff) consider coalescing with previous and next block
    header->ptr = ioa->free_list;
    ioa->free_list = header;

    mxr_mutex_unlock(&ioa->mutex);
}

mx_paddr_t io_virt_to_phys(io_alloc_t* ioa, mx_vaddr_t virt_addr) {
    return virt_addr - ioa->virt_offset;
}

mx_vaddr_t io_phys_to_virt(io_alloc_t* ioa, mx_paddr_t phys_addr) {
    return phys_addr + ioa->virt_offset;
}
