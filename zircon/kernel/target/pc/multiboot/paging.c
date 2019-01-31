// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trampoline.h"

#include <cpuid.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <arch/x86/page_tables/constants.h>
#include <arch/x86/registers.h>

// Set up minimal page tables for 64-bit mode.  These map the low
// 4G of address space directly to the low 4G of physical memory.

static uint32_t get_cr0(void) {
    uint32_t cr0;
    __asm__("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}

static void set_cr0(uint32_t cr0) {
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
}

static void set_cr3(void* cr3) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

static uint32_t get_cr4(void) {
    uint32_t cr4;
    __asm__("mov %%cr4, %0" : "=r"(cr4));
    return cr4;
}

static void set_cr4(uint32_t cr4) {
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
}

static uint64_t read_msr(uint32_t msr) {
    uint64_t value;
    __asm__("rdmsr" : "=A"(value) : "c"(msr));
    return value;
}

static void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" :: "c"(msr), "A"(value));
}

static bool have_page1gb;

typedef union page_table {
    alignas(4096) uint64_t pml4e[512];
    alignas(4096) uint64_t pdpte[512];
    alignas(4096) uint64_t pde[512];
} page_table_t;

static page_table_t* page_table_memory_start;
static page_table_t* page_table_memory_end;

static page_table_t* get_page_table(void) {
    if (unlikely(page_table_memory_start >= page_table_memory_end)) {
        panic("ran out of page table memory");
    }
    return page_table_memory_start++;
}

static uint64_t get_pdpte(unsigned int idx) {
    // Each PDPTE covers 1G.
    if (have_page1gb) {
        // A single entry direct-maps 1G.
        return (((uint64_t)idx << 30) |
                X86_MMU_PG_P | X86_MMU_PG_RW | X86_MMU_PG_PS);
    } else {
        // A single entry indirects through a PDE table.
        page_table_t* pdpt = get_page_table();
        for (size_t i = 0; i < 512; ++i) {
            // Each PDE covers 2M.
            pdpt->pde[i] = (((uint64_t)i << 21) |
                            X86_MMU_PG_P | X86_MMU_PG_RW | X86_MMU_PG_PS);
        }
        return ((uint64_t)(uintptr_t)pdpt | X86_MMU_PG_P | X86_MMU_PG_RW);
    }
}

// The whole PDPT covers 512G, so we only need the one.
static uint64_t get_pml4e(void) {
    page_table_t* pdpt = get_page_table();
    // Each PDPTE covers 1G, so we need four of those.
    for (unsigned int i = 0; i < 4; ++i) {
        pdpt->pdpte[i] = get_pdpte(i);
    }
    memset(&pdpt->pdpte[4], 0,
           sizeof(*pdpt) - offsetof(page_table_t, pdpte[4]));
    return (uint64_t)(uintptr_t)pdpt | X86_MMU_PG_P | X86_MMU_PG_RW;
}

static page_table_t* get_pml4(void) {
    page_table_t* pml4 = get_page_table();
    // The top-level PML4 just needs one PML4E to point to the PDPT.
    pml4->pml4e[0] = get_pml4e();
    memset(&pml4->pml4e[1], 0,
           sizeof(*pml4) - offsetof(page_table_t, pml4e[1]));
    return pml4;
}

void enable_64bit_paging(uintptr_t start, uintptr_t end) {
    // Use available memory for page tables.
    page_table_memory_start = (void*)((start + 4096 - 1) & -4096u);
    page_table_memory_end = (void*)(end & -4096u);

    // Determine if 1G pages are available.
    {
        uint32_t a, b, c, d;
        __cpuid(0x80000001u, a, b, c, d);
        have_page1gb = (d & (1u << 26)) != 0;
    }

    // Use the 64-bit (PAE) page table format.
    // This is required in 64-bit (Long) mode.
    set_cr4(get_cr4() | X86_CR4_PAE);

    // Enable 64-bit (Long) mode.
    write_msr(X86_MSR_IA32_EFER, read_msr(X86_MSR_IA32_EFER) | X86_EFER_LME);

    // Install the page tables.
    set_cr3(get_pml4());

    // Enable paging.
    // Hereafter we're using the direct-mapped page tables just built.
    set_cr0(get_cr0() | X86_CR0_PG);
}
