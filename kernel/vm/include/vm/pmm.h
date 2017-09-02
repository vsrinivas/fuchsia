// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <sys/types.h>
#include <vm/page.h>

// physical allocator
typedef struct pmm_arena_info {
    char name[16];

    uint flags;
    uint priority;

    paddr_t base;
    size_t size;
} pmm_arena_info_t;

#define PMM_ARENA_FLAG_KMAP (0x1) // this arena is already mapped and useful for kallocs

// Add a pre-filled memory arena to the physical allocator.
status_t pmm_add_arena(const pmm_arena_info_t* arena) __NONNULL((1));

// flags for allocation routines below
#define PMM_ALLOC_FLAG_ANY (0x0)  // no restrictions on which arena to allocate from
#define PMM_ALLOC_FLAG_KMAP (0x1) // allocate only from arenas marked KMAP

// Allocate count pages of physical memory, adding to the tail of the passed list.
// The list must be initialized.
// Returns the number of pages allocated.
size_t pmm_alloc_pages(size_t count, uint alloc_flags, struct list_node* list) __NONNULL((3));

// Allocate a single page of physical memory.
vm_page_t* pmm_alloc_page(uint alloc_flags, paddr_t* pa);

// Allocate a specific range of physical pages, adding to the tail of the passed list.
// Returns the number of pages allocated.
size_t pmm_alloc_range(paddr_t address, size_t count, struct list_node* list);

// Allocate a run of contiguous pages, aligned on log2 byte boundary (0-31)
// If the optional physical address pointer is passed, return the address.
// If the optional list is passed, append the allocate page structures to the tail of the list.
size_t pmm_alloc_contiguous(size_t count, uint alloc_flags, uint8_t align_log2, paddr_t* pa,
                            struct list_node* list);

// Free a list of physical pages.
// Returns the number of pages freed.
size_t pmm_free(struct list_node* list) __NONNULL((1));

// Helper routine for the above.
size_t pmm_free_page(vm_page_t* page) __NONNULL((1));

// Return count of unallocated physical pages in system
size_t pmm_count_free_pages(void);

// Return amount of physical memory in system, in bytes.
size_t pmm_count_total_bytes(void);

// Counts the number of pages in every state. For every page in every arena,
// increments the corresponding VM_PAGE_STATE_*-indexed entry of
// |state_count|. Does not zero out the entries first.
void pmm_count_total_states(size_t state_count[_VM_PAGE_STATE_COUNT]);

// Allocate a run of pages out of the kernel area and return the pointer in kernel space.
// If the optional list is passed, append the allocate page structures to the tail of the list.
// If the optional physical address pointer is passed, return the address.
void* pmm_alloc_kpages(size_t count, struct list_node* list, paddr_t* pa);

// Same as above but a single page at a time
void* pmm_alloc_kpage(paddr_t* pa, vm_page_t** p);

size_t pmm_free_kpages(void* ptr, size_t count);

// physical to virtual
void* paddr_to_kvaddr(paddr_t pa);

// virtual to physical
paddr_t vaddr_to_paddr(const void* va);

// vm_page_t to physical address
paddr_t vm_page_to_paddr(const vm_page_t* page);

// paddr to vm_page_t
vm_page_t* paddr_to_vm_page(paddr_t addr);
