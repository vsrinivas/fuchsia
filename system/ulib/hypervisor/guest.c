// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <hypervisor/guest.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

static const uint32_t kMapFlags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
static const size_t kMaxSize = 512ull << 30;
static const size_t kMinSize = 4 * (4 << 10);

#if __x86_64__
static const size_t kPml4PageSize = 512ull << 30;
static const size_t kPdpPageSize = 1 << 30;
static const size_t kPdPageSize = 2 << 20;
static const size_t kPtPageSize = 4 << 10;
static const size_t kPtesPerPage = PAGE_SIZE / sizeof(uint64_t);

enum {
    X86_PTE_P    = 0x01,    /* P    Valid           */
    X86_PTE_RW   = 0x02,    /* R/W  Read/Write      */
    X86_PTE_PS   = 0x80,    /* PS   Page size       */
};

/**
 * Create all page tables for a given page size.
 *
 * @param addr The mapped address of where to write the page table. Must be page-aligned.
 * @param size The size of memory to map.
 * @param l1_page_size The size of pages at this level.
 * @param l1_pte_off The offset of this page table, relative to the start of memory.
 * @param aspace_off The address space offset, used to keep track of mapped address space.
 * @param has_page Whether this level of the page table has associated pages.
 */
static uintptr_t page_table(uintptr_t addr, size_t size, size_t l1_page_size, uintptr_t l1_pte_off,
                            uint64_t* aspace_off, bool has_page) {
    size_t l1_ptes = (size + l1_page_size - 1) / l1_page_size;
    bool has_l0_aspace = size % l1_page_size != 0;
    size_t l1_pages = (l1_ptes + kPtesPerPage - 1) / kPtesPerPage;
    uintptr_t l0_pte_off = l1_pte_off + l1_pages * PAGE_SIZE;

    uint64_t* pt = (uint64_t*)(addr + l1_pte_off);
    for (size_t i = 0; i < l1_ptes; i++) {
        if (has_page && (!has_l0_aspace || i < l1_ptes - 1)) {
            pt[i] = *aspace_off | X86_PTE_P | X86_PTE_RW | X86_PTE_PS;
            *aspace_off += l1_page_size;
        } else {
            if (i > 0 && (i % kPtesPerPage == 0))
                l0_pte_off += PAGE_SIZE;
            pt[i] = l0_pte_off | X86_PTE_P | X86_PTE_RW;
        }
    }

    return l0_pte_off;
}
#endif // __x86_64__

mx_status_t guest_create_phys_mem(uintptr_t* addr, size_t size, mx_handle_t* phys_mem) {
    if (size % PAGE_SIZE != 0)
        return ERR_INVALID_ARGS;
    if (size > kMaxSize || size < kMinSize)
        return ERR_OUT_OF_RANGE;

    mx_status_t status = mx_vmo_create(size, 0, phys_mem);
    if (status != NO_ERROR)
        return status;

    status = mx_vmar_map(mx_vmar_root_self(), 0, *phys_mem, 0, size, kMapFlags, addr);
    if (status != NO_ERROR) {
        mx_handle_close(*phys_mem);
        return status;
    }

    return NO_ERROR;
}

mx_status_t guest_create_page_table(uintptr_t addr, size_t size, uintptr_t* pte_off) {
    if (size % PAGE_SIZE != 0)
        return ERR_INVALID_ARGS;
    if (size > kMaxSize || size < kMinSize)
        return ERR_OUT_OF_RANGE;

#if __x86_64__
    uint64_t aspace_off = 0;
    *pte_off = 0;
    *pte_off = page_table(addr, size - aspace_off, kPml4PageSize, *pte_off, &aspace_off, false);
    *pte_off = page_table(addr, size - aspace_off, kPdpPageSize, *pte_off, &aspace_off, true);
    *pte_off = page_table(addr, size - aspace_off, kPdPageSize, *pte_off, &aspace_off, true);
    *pte_off = page_table(addr, size - aspace_off, kPtPageSize, *pte_off, &aspace_off, true);
    return NO_ERROR;
#else // __x86_64__
    return ERR_NOT_SUPPORTED;
#endif // __x86_64__
}

mx_status_t guest_create(mx_handle_t hypervisor, mx_handle_t phys_mem, mx_handle_t* serial_fifo,
                         mx_handle_t* guest) {
    mx_handle_t guest_fifo;
    mx_status_t status = mx_fifo_create(PAGE_SIZE, 1, 0, &guest_fifo, serial_fifo);
    if (status != NO_ERROR)
        return status;

    mx_handle_t create_args[2] = { phys_mem, guest_fifo };
    return mx_hypervisor_op(hypervisor, MX_HYPERVISOR_OP_GUEST_CREATE,
                            create_args, sizeof(create_args), guest, sizeof(*guest));
}
