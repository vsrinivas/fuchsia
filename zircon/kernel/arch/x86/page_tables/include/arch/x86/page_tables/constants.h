// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

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
#define X86_DIRTY_ACCESS_MASK   0xf9f

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

/* on x86-64 physical memory is mapped at the base of the kernel address space */
#define X86_PHYS_TO_VIRT(x)     ((uintptr_t)(x) + KERNEL_ASPACE_BASE)
#define X86_VIRT_TO_PHYS(x)     ((uintptr_t)(x) - KERNEL_ASPACE_BASE)

#define IS_PAGE_PRESENT(pte)    ((pte) & X86_MMU_PG_P)
#define IS_LARGE_PAGE(pte)      ((pte) & X86_MMU_PG_PS)

// NOTE(abdulla): We assume that PT and EPT paging levels match, specifically:
// - PML4 entries refer to 512GB pages
// - PDP entries refer to 1GB pages
// - PD entries refer to 2MB pages
// - PT entries refer to 4KB pages
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
