// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

/* top level defines for the x86 mmu */
/* NOTE: the top part can be included from assembly */
#define X86_MMU_PG_P            0x0001          /* P    Valid                   */
#define X86_MMU_PG_RW           0x0002          /* R/W  Read/Write              */
#define X86_MMU_PG_U            0x0004          /* U/S  User/Supervisor         */
#define X86_MMU_PG_WT           0x0008          /* WT   Write-through           */
#define X86_MMU_PG_CD           0x0010          /* CD   Cache disable           */
#define X86_MMU_PG_A            0x0020          /* A    Accessed                */
#define X86_MMU_PG_D            0x0040          /* D    Dirty                   */
#define X86_MMU_PG_PS           0x0080          /* PS   Page size (0=4k,1=4M)   */
#define X86_MMU_PG_PTE_PAT      0x0080          /* PAT  PAT index for 4k pages  */
#define X86_MMU_PG_LARGE_PAT    0x1000          /* PAT  PAT index otherwise     */
#define X86_MMU_PG_G            0x0100          /* G    Global                  */
#define X86_MMU_CLEAR           0x0
#define X86_DIRTY_ACCESS_MASK   0xf9f

#define X86_EPT_R               (1u << 0)       /* R    Read        */
#define X86_EPT_W               (1u << 1)       /* W    Write       */
#define X86_EPT_X               (1u << 2)       /* X    Execute     */
#define X86_EPT_A               (1u << 8)       /* A    Accessed    */
#define X86_EPT_D               (1u << 9)       /* D    Dirty       */

/* From Volume 3, Section 28.2.6: EPT and Memory Typing */
#define X86_EPT_WB              (6u << 3)       /* WB   Write-back memory type  */

/* Page Attribute Table memory types, defined in Table 11-10 of Intel 3A */
#define X86_PAT_UC              0x00 /* Uncached */
#define X86_PAT_WC              0x01 /* Write-combining */
#define X86_PAT_WT              0x04 /* Write-through */
#define X86_PAT_WP              0x05 /* Write protected */
#define X86_PAT_WB              0X06 /* Write-back */
#define X86_PAT_UC_             0x07 /* Weakly Uncached (can be overrided by a
                                      * WC MTRR setting) */

/* Macros for converting from PAT index to appropriate page table flags.  Note
 * that the smallest level has one of the flags at a different bit index, so we
 * need two versions of each macro. */
#define _X86_PAT_COMMON_SELECTOR(x) ((((x) & 0x2) ? X86_MMU_PG_CD : 0) | \
                                     (((x) & 0x1) ? X86_MMU_PG_WT : 0))
#define X86_PAT_PTE_SELECTOR(x) ((((x) & 0x4) ? X86_MMU_PG_PTE_PAT : 0) | \
                                 _X86_PAT_COMMON_SELECTOR(x))
#define X86_PAT_LARGE_SELECTOR(x) ((((x) & 0x4) ? X86_MMU_PG_LARGE_PAT : 0) | \
                                   _X86_PAT_COMMON_SELECTOR(x))

#define X86_MMU_PTE_PAT_MASK X86_PAT_PTE_SELECTOR(0x7)
#define X86_MMU_LARGE_PAT_MASK X86_PAT_LARGE_SELECTOR(0x7)

/* Our configuration for the PAT indexes.  This must be kept in sync with the
 * selector definitions below it.  For safety, it is important to ensure that
 * the default mode is less cached than our substitution.  This ensures that
 * any mappings defined before we switch all CPUs to this new map will still
 * function correctly. */
#define X86_PAT_INDEX0  X86_PAT_WB  /* default */
#define X86_PAT_INDEX1  X86_PAT_WT  /* default */
#define X86_PAT_INDEX2  X86_PAT_UC_ /* default */
#define X86_PAT_INDEX3  X86_PAT_UC  /* default */
#define X86_PAT_INDEX4  X86_PAT_WB  /* default */
#define X86_PAT_INDEX5  X86_PAT_WT  /* default */
#define X86_PAT_INDEX6  X86_PAT_UC_ /* default */
#define X86_PAT_INDEX7  X86_PAT_WC  /* UC by default */

/* These assume our defined PAT entries.  We need to update these if we decide
 * to change them PAT entries */
#define X86_MMU_PTE_PAT_WRITEBACK               X86_PAT_PTE_SELECTOR(0)
#define X86_MMU_PTE_PAT_WRITETHROUGH            X86_PAT_PTE_SELECTOR(1)
#define X86_MMU_PTE_PAT_UNCACHABLE              X86_PAT_PTE_SELECTOR(3)
#define X86_MMU_PTE_PAT_WRITE_COMBINING         X86_PAT_PTE_SELECTOR(7)
#define X86_MMU_LARGE_PAT_WRITEBACK             X86_PAT_LARGE_SELECTOR(0)
#define X86_MMU_LARGE_PAT_WRITETHROUGH          X86_PAT_LARGE_SELECTOR(1)
#define X86_MMU_LARGE_PAT_UNCACHABLE            X86_PAT_LARGE_SELECTOR(3)
#define X86_MMU_LARGE_PAT_WRITE_COMBINING       X86_PAT_LARGE_SELECTOR(7)

/* default flags for inner page directory entries */
#define X86_KERNEL_PD_FLAGS (X86_MMU_PG_RW | X86_MMU_PG_P)

/* default flags for 2MB/4MB/1GB page directory entries */
#define X86_KERNEL_PD_LP_FLAGS (X86_MMU_PG_G | X86_MMU_PG_PS | X86_MMU_PG_RW | X86_MMU_PG_P)

#define IS_PAGE_PRESENT(pte)    ((pte) & X86_MMU_PG_P)
#define IS_LARGE_PAGE(pte)      ((pte) & X86_MMU_PG_PS)

#define X86_MMU_PG_NX           (1UL << 63)

// NOTE(abdulla): We assume that PT and EPT paging levels match, specifically:
// - PML4 entries refer to 512GB pages
// - PDP entries refer to 1GB pages
// - PD entries refer to 2MB pages
// - PT entries refer to 4KB pages

#define X86_PAGING_LEVELS       4
#define PML4_SHIFT              39
#define PDP_SHIFT               30
#define PD_SHIFT                21
#define PT_SHIFT                12
#define ADDR_OFFSET             9
#define PDPT_ADDR_OFFSET        2
#define NO_OF_PT_ENTRIES        512

#define X86_FLAGS_MASK          (0x8000000000000ffful)
#define X86_LARGE_FLAGS_MASK    (0x8000000000001ffful)
#define X86_PDPT_ADDR_MASK      (0x00000000ffffffe0ul)
#define X86_HUGE_PAGE_FRAME     (0x000fffffc0000000ul)
#define X86_LARGE_PAGE_FRAME    (0x000fffffffe00000ul)
#define X86_PG_FRAME            (0x000ffffffffff000ul)
#define PAGE_OFFSET_MASK_4KB    ((1ul << PT_SHIFT) - 1)
#define PAGE_OFFSET_MASK_LARGE  ((1ul << PD_SHIFT) - 1)
#define PAGE_OFFSET_MASK_HUGE   ((1ul << PDP_SHIFT) - 1)

#define VADDR_TO_PML4_INDEX(vaddr) ((vaddr) >> PML4_SHIFT) & ((1ul << ADDR_OFFSET) - 1)
#define VADDR_TO_PDP_INDEX(vaddr)  ((vaddr) >> PDP_SHIFT) & ((1ul << ADDR_OFFSET) - 1)

#define VADDR_TO_PD_INDEX(vaddr)  ((vaddr) >> PD_SHIFT) & ((1ul << ADDR_OFFSET) - 1)
#define VADDR_TO_PT_INDEX(vaddr)  ((vaddr) >> PT_SHIFT) & ((1ul << ADDR_OFFSET) - 1)

/* on both x86-32 and x86-64 physical memory is mapped at the base of the kernel address space */
#define X86_PHYS_TO_VIRT(x)     ((uintptr_t)(x) + KERNEL_ASPACE_BASE)
#define X86_VIRT_TO_PHYS(x)     ((uintptr_t)(x) - KERNEL_ASPACE_BASE)

/* page fault error code flags */
#define PFEX_P      (1<<0)
#define PFEX_W      (1<<1)
#define PFEX_U      (1<<2)
#define PFEX_RSV    (1<<3)
#define PFEX_I      (1<<4)
#define PFEX_PK     (1<<5)
#define PFEX_SGX    (1<<15)

/* C defines below */
#ifndef ASSEMBLY

#include <sys/types.h>
#include <magenta/compiler.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

__BEGIN_CDECLS

/* Different page table levels in the page table mgmt hirerachy */
enum page_table_levels {
    PT_L,
    PD_L,
    PDP_L,
    PML4_L,
};

#define MAX_PAGING_LEVEL        (enum page_table_levels)(X86_PAGING_LEVELS - 1)

struct map_range {
    vaddr_t start_vaddr;
    paddr_t start_paddr; /* Physical address in the PAE mode is 32 bits wide */
    size_t size;
};

typedef uint64_t pt_entry_t;
#define PRIxPTE PRIx64

typedef pt_entry_t arch_flags_t;

bool x86_is_vaddr_canonical(vaddr_t vaddr);

void x86_mmu_percpu_init(void);
void x86_mmu_early_init(void);
void x86_mmu_init(void);

paddr_t x86_kernel_cr3(void);

__END_CDECLS

#endif // !ASSEMBLY
