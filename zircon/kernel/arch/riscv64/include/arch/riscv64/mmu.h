// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_MMU_H_
#define ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_MMU_H_

#include <arch/defines.h>
#include <arch/kernel_aspace.h>
#include <lib/zircon-internal/macros.h>

#define MMU_LX_X(page_shift, level) ((4 - (level)) * ((page_shift) - 3) + 3)
#define MMU_KERNEL_PAGE_SIZE_SHIFT      (PAGE_SIZE_SHIFT)
#define MMU_USER_SIZE_SHIFT 48

typedef uintptr_t pte_t;

#define RISCV64_MMU_PT_LEVELS 4
#define RISCV64_MMU_PT_SHIFT  9
#define RISCV64_MMU_PT_ENTRIES 512 // 1 << PT_SHIFT
#define RISCV64_MMU_CANONICAL_MASK ((1UL << 48) - 1)
#define RISCV64_MMU_PPN_BITS 56

// page table bits
#define RISCV64_PTE_V         (1 << 0) // valid
#define RISCV64_PTE_R         (1 << 1) // read
#define RISCV64_PTE_W         (1 << 2) // write
#define RISCV64_PTE_X         (1 << 3) // execute
#define RISCV64_PTE_PERM_MASK (0x7 << 1)
#define RISCV64_PTE_U         (1 << 4) // user
#define RISCV64_PTE_G         (1 << 5) // global
#define RISCV64_PTE_A         (1 << 6) // accessed
#define RISCV64_PTE_D         (1 << 7) // dirty
#define RISCV64_PTE_RSW_MASK  (3 << 8) // reserved for software
#define RISCV64_PTE_PPN_SHIFT (10)
#define RISCV64_PTE_PPN_MASK  (((1UL << (RISCV64_MMU_PPN_BITS - PAGE_SIZE_SHIFT)) - 1) << RISCV64_PTE_PPN_SHIFT)

// riscv PPN is stored shifed over 2 from the natural alignment
#define RISCV64_PTE_PPN(pte) (((pte) & RISCV64_PTE_PPN_MASK) << (PAGE_SIZE_SHIFT - RISCV64_PTE_PPN_SHIFT))
#define RISCV64_PTE_PPN_TO_PTE(paddr) (((paddr) >> PAGE_SIZE_SHIFT) << RISCV64_PTE_PPN_SHIFT)

// SATP register, contains the current mmu mode, address space id, and
// pointer to root page table
#define RISCV64_SATP_MODE_NONE (0)
#define RISCV64_SATP_MODE_SV32 (1)
#define RISCV64_SATP_MODE_SV39 (8)
#define RISCV64_SATP_MODE_SV48 (9)
#define RISCV64_SATP_MODE_SV57 (10)
#define RISCV64_SATP_MODE_SV64 (11)

#define RISCV64_SATP_MODE_SHIFT (60)
#define RISCV64_SATP_ASID_SHIFT (44)
#define RISCV64_SATP_ASID_SIZE  (16)
#define RISCV64_SATP_ASID_MASK  ((1UL << RISCV64_SATP_ASID_SIZE) - 1)

const size_t MMU_RISCV64_ASID_BITS = 16;
const uint16_t MMU_RISCV64_GLOBAL_ASID = (1u << MMU_RISCV64_ASID_BITS) - 1;
const uint16_t MMU_RISCV64_UNUSED_ASID = 0;
const uint16_t MMU_RISCV64_FIRST_USER_ASID = 1;
const uint16_t MMU_RISCV64_MAX_USER_ASID = MMU_RISCV64_GLOBAL_ASID - 1;

#endif  // ZIRCON_KERNEL_ARCH_RISCV64_INCLUDE_ARCH_RISCV64_MMU_H_
