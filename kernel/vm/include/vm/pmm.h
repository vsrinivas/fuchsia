// Copyright 2017 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>
#include <vm/page.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// physical allocator
typedef struct pmm_arena_info {
    char name[16];

    uint flags;
    uint priority;

    paddr_t base;
    size_t size;
} pmm_arena_info_t;

#define PMM_ARENA_FLAG_LO_MEM (0x1) // this arena is contained within architecturally-defined 'low memory'

// Add a pre-filled memory arena to the physical allocator.
// The arena data will be copied.
zx_status_t pmm_add_arena(const pmm_arena_info_t* arena);

// flags for allocation routines below
#define PMM_ALLOC_FLAG_ANY (0x0)    // no restrictions on which arena to allocate from
#define PMM_ALLOC_FLAG_LO_MEM (0x1) // allocate only from arenas marked LO_MEM

// Allocate count pages of physical memory, adding to the tail of the passed list.
// The list must be initialized.
// Returns the number of pages allocated.
size_t pmm_alloc_pages(size_t count, uint alloc_flags, list_node* list);

// Allocate a single page of physical memory.
vm_page_t* pmm_alloc_page(uint alloc_flags, paddr_t* pa);

// Allocate a specific range of physical pages, adding to the tail of the passed list.
// Returns the number of pages allocated.
size_t pmm_alloc_range(paddr_t address, size_t count, list_node* list);

// Allocate a run of contiguous pages, aligned on log2 byte boundary (0-31)
// If the optional physical address pointer is passed, return the address.
// If the optional list is passed, append the allocate page structures to the tail of the list.
size_t pmm_alloc_contiguous(size_t count, uint alloc_flags, uint8_t align_log2, paddr_t* pa,
                            list_node* list);

// Free a list of physical pages.
void pmm_free(list_node* list);

// Free a single page.
void pmm_free_page(vm_page_t* page);

// Return count of unallocated physical pages in system.
uint64_t pmm_count_free_pages();

// Return amount of physical memory in system, in bytes.
uint64_t pmm_count_total_bytes();

// Counts the number of pages in every state. For every page in every arena,
// increments the corresponding VM_PAGE_STATE_*-indexed entry of
// |state_count|. Does not zero out the entries first.
void pmm_count_total_states(size_t state_count[VM_PAGE_STATE_COUNT_]);

// virtual to physical
paddr_t vaddr_to_paddr(const void* va);

// paddr to vm_page_t
vm_page_t* paddr_to_vm_page(paddr_t addr);
