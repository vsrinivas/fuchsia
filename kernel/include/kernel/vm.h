// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

/* some assembly #defines, need to match the structure below */
#define __MMU_INITIAL_MAPPING_PHYS_OFFSET 0
#define __MMU_INITIAL_MAPPING_VIRT_OFFSET 8
#define __MMU_INITIAL_MAPPING_SIZE_OFFSET 16
#define __MMU_INITIAL_MAPPING_FLAGS_OFFSET 24
#define __MMU_INITIAL_MAPPING_SIZE 40

/* flags for initial mapping struct */
#define MMU_INITIAL_MAPPING_TEMPORARY (0x1)
#define MMU_INITIAL_MAPPING_FLAG_UNCACHED (0x2)
#define MMU_INITIAL_MAPPING_FLAG_DEVICE (0x4)
#define MMU_INITIAL_MAPPING_FLAG_DYNAMIC (0x8) /* entry has to be patched up by platform_reset */

#ifndef ASSEMBLY

#include <arch.h>
#include <arch/mmu.h>
#include <assert.h>
#include <kernel/vm/page.h>
#include <list.h>
#include <magenta/compiler.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

__BEGIN_CDECLS

#define PAGE_ALIGN(x) ALIGN((x), PAGE_SIZE)
#define ROUNDUP_PAGE_SIZE(x) ROUNDUP((x), PAGE_SIZE)
#define IS_PAGE_ALIGNED(x) IS_ALIGNED((x), PAGE_SIZE)

struct mmu_initial_mapping {
    paddr_t phys;
    vaddr_t virt;
    size_t size;
    unsigned int flags;
    const char* name;
};

/* Assert that the assembly macros above match this struct. */
static_assert(__offsetof(struct mmu_initial_mapping, phys) == __MMU_INITIAL_MAPPING_PHYS_OFFSET, "");
static_assert(__offsetof(struct mmu_initial_mapping, virt) == __MMU_INITIAL_MAPPING_VIRT_OFFSET, "");
static_assert(__offsetof(struct mmu_initial_mapping, size) == __MMU_INITIAL_MAPPING_SIZE_OFFSET, "");
static_assert(__offsetof(struct mmu_initial_mapping, flags) == __MMU_INITIAL_MAPPING_FLAGS_OFFSET, "");
static_assert(sizeof(struct mmu_initial_mapping) == __MMU_INITIAL_MAPPING_SIZE, "");

/* Platform or target must fill out one of these to set up the initial memory map
 * for kernel and enough IO space to boot.
 */
extern struct mmu_initial_mapping mmu_initial_mappings[];

/* kernel address space */
#ifndef KERNEL_ASPACE_BASE
#define KERNEL_ASPACE_BASE ((vaddr_t)0x80000000UL)
#endif
#ifndef KERNEL_ASPACE_SIZE
#define KERNEL_ASPACE_SIZE ((vaddr_t)0x80000000UL)
#endif

static_assert(KERNEL_ASPACE_BASE + (KERNEL_ASPACE_SIZE - 1) > KERNEL_ASPACE_BASE, "");

static inline bool is_kernel_address(vaddr_t va) {
    return (va >= (vaddr_t)KERNEL_ASPACE_BASE &&
            va <= ((vaddr_t)KERNEL_ASPACE_BASE + ((vaddr_t)KERNEL_ASPACE_SIZE - 1)));
}

/* user address space, defaults to below kernel space with a 16MB guard gap on either side */
#ifndef USER_ASPACE_BASE
#define USER_ASPACE_BASE ((vaddr_t)0x01000000UL)
#endif
#ifndef USER_ASPACE_SIZE
#define USER_ASPACE_SIZE ((vaddr_t)KERNEL_ASPACE_BASE - USER_ASPACE_BASE - 0x01000000UL)
#endif

static_assert(USER_ASPACE_BASE + (USER_ASPACE_SIZE - 1) > USER_ASPACE_BASE, "");

static inline bool is_user_address(vaddr_t va) {
    return (va >= USER_ASPACE_BASE && va <= (USER_ASPACE_BASE + (USER_ASPACE_SIZE - 1)));
}

static inline bool is_user_address_range(vaddr_t va, size_t len) {
    return va + len >= va &&
           is_user_address(va) &&
           (len == 0 || is_user_address(va + len - 1));
}

/* physical allocator */
typedef struct pmm_arena_info {
    const char* name;

    uint flags;
    uint priority;

    paddr_t base;
    size_t size;
} pmm_arena_info_t;

#define PMM_ARENA_FLAG_KMAP (0x1) /* this arena is already mapped and useful for kallocs */

/* Add a pre-filled memory arena to the physical allocator. */
status_t pmm_add_arena(const pmm_arena_info_t* arena) __NONNULL((1));

/* flags for allocation routines below */
#define PMM_ALLOC_FLAG_ANY (0x0)  /* no restrictions on which arena to allocate from */
#define PMM_ALLOC_FLAG_KMAP (0x1) /* allocate only from arenas marked KMAP */

/* Allocate count pages of physical memory, adding to the tail of the passed list.
 * The list must be initialized.
 * Returns the number of pages allocated.
 */
size_t pmm_alloc_pages(size_t count, uint alloc_flags, struct list_node* list) __NONNULL((3));

/* Allocate a single page of physical memory.
 */
vm_page_t* pmm_alloc_page(uint alloc_flags, paddr_t* pa);

/* Allocate a specific range of physical pages, adding to the tail of the passed list.
 * Returns the number of pages allocated.
 */
size_t pmm_alloc_range(paddr_t address, size_t count, struct list_node* list);

/* Allocate a run of contiguous pages, aligned on log2 byte boundary (0-31)
 * If the optional physical address pointer is passed, return the address.
 * If the optional list is passed, append the allocate page structures to the tail of the list.
 */
size_t pmm_alloc_contiguous(size_t count, uint alloc_flags, uint8_t align_log2, paddr_t* pa,
                            struct list_node* list);

/* Free a list of physical pages.
 * Returns the number of pages freed.
 */
size_t pmm_free(struct list_node* list) __NONNULL((1));

/* Helper routine for the above. */
size_t pmm_free_page(vm_page_t* page) __NONNULL((1));

/* Return count of unallocated physical pages in system */
size_t pmm_count_free_pages(void);

// Return amount of physical memory in system, in bytes.
size_t pmm_count_total_bytes(void);

/* Allocate a run of pages out of the kernel area and return the pointer in kernel space.
 * If the optional list is passed, append the allocate page structures to the tail of the list.
 * If the optional physical address pointer is passed, return the address.
 */
void* pmm_alloc_kpages(size_t count, struct list_node* list, paddr_t* pa);

/* Same as above but a single page at a time */
void* pmm_alloc_kpage(paddr_t* pa, vm_page_t** p);

size_t pmm_free_kpages(void* ptr, size_t count);

/* physical to virtual */
void* paddr_to_kvaddr(paddr_t pa);

/* virtual to physical */
paddr_t vaddr_to_paddr(const void* va);

/* vm_page_t to physical address */
paddr_t vm_page_to_paddr(const vm_page_t* page);

/* paddr to vm_page_t */
vm_page_t* paddr_to_vm_page(paddr_t addr);

/* C friendly opaque handle to the internals of the VMM.
 * Never defined, just used as a handle for C apis.
 */
typedef struct vmm_aspace vmm_aspace_t;

/* grab a handle to the kernel address space */
vmm_aspace_t* vmm_get_kernel_aspace(void);

/* virtual to container address space */
struct vmm_aspace* vaddr_to_aspace(const void* ptr);

/* retrieve the arch-specific information for this aspace */
arch_aspace_t* vmm_get_arch_aspace(vmm_aspace_t* aspace);

/* For region creation routines */
#define VMM_FLAG_VALLOC_SPECIFIC (1u << 0) /* allocate at specific address */
#define VMM_FLAG_COMMIT (1u << 1)          /* commit memory up front (no demand paging) */

/* internal kernel routines below, do not call directly */

/* internal routine by the scheduler to swap mmu contexts */
void vmm_context_switch(vmm_aspace_t* oldspace, vmm_aspace_t* newaspace);

/* set the current user aspace as active on the current thread.
   NULL is a valid argument, which unmaps the current user address space */
void vmm_set_active_aspace(vmm_aspace_t* aspace);

/* page fault flags */
#define VMM_PF_FLAG_WRITE (1u << 0)
#define VMM_PF_FLAG_USER (1u << 1)
#define VMM_PF_FLAG_INSTRUCTION (1u << 2)
#define VMM_PF_FLAG_NOT_PRESENT (1u << 3)

#define VMM_PF_FLAG_HW_FAULT (1u << 4) /* hardware is requesting a fault */
#define VMM_PF_FLAG_SW_FAULT (1u << 5) /* software fault */
#define VMM_PF_FLAG_FAULT_MASK (VMM_PF_FLAG_HW_FAULT | VMM_PF_FLAG_SW_FAULT)

/* convenience routine for convering page fault flags to a string */
static const char* vmm_pf_flags_to_string(uint pf_flags, char str[5]) {
    str[0] = (pf_flags & VMM_PF_FLAG_WRITE) ? 'w' : 'r';
    str[1] = (pf_flags & VMM_PF_FLAG_USER) ? 'u' : 's';
    str[2] = (pf_flags & VMM_PF_FLAG_INSTRUCTION) ? 'i' : 'd';
    str[3] = (pf_flags & VMM_PF_FLAG_NOT_PRESENT) ? 'n' : 'p';
    str[4] = '\0';

    return str;
}

/* page fault handler, called during page fault context, with interrupts enabled */
status_t vmm_page_fault_handler(vaddr_t addr, uint pf_flags);

/* return a pointer to the zero page */
static inline vm_page_t* vm_get_zero_page(void) {
    extern vm_page_t* zero_page;
    return zero_page;
}

/* return the physical address of the zero page */
static inline paddr_t vm_get_zero_page_paddr(void) {
    extern paddr_t zero_page_paddr;

    return zero_page_paddr;
}

__END_CDECLS

#endif // !ASSEMBLY
