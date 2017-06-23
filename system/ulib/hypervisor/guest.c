// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <hypervisor/guest.h>
#include <magenta/boot/bootdata.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

typedef struct e820entry {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} __PACKED e820entry_t;

static const uint32_t kE820Ram = 1;
static const uint32_t kE820Reserved = 2;

static const uint64_t kAddr1mb      = 0x0000000000100000;
static const uint64_t kAddr3500mb   = 0x00000000e0000000;
static const uint64_t kAddr4000mb   = 0x0000000100000000;

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
 * @param map_flags Flags added to any descriptors directly mapping pages.
 */
static uintptr_t page_table(uintptr_t addr, size_t size, size_t l1_page_size, uintptr_t l1_pte_off,
                            uint64_t* aspace_off, bool has_page, uint64_t map_flags) {
    size_t l1_ptes = (size + l1_page_size - 1) / l1_page_size;
    bool has_l0_aspace = size % l1_page_size != 0;
    size_t l1_pages = (l1_ptes + kPtesPerPage - 1) / kPtesPerPage;
    uintptr_t l0_pte_off = l1_pte_off + l1_pages * PAGE_SIZE;

    uint64_t* pt = (uint64_t*)(addr + l1_pte_off);
    for (size_t i = 0; i < l1_ptes; i++) {
        if (has_page && (!has_l0_aspace || i < l1_ptes - 1)) {
            pt[i] = *aspace_off | X86_PTE_P | X86_PTE_RW | map_flags;
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
        return MX_ERR_INVALID_ARGS;
    if (size > kMaxSize || size < kMinSize)
        return MX_ERR_OUT_OF_RANGE;

    mx_status_t status = mx_vmo_create(size, 0, phys_mem);
    if (status != MX_OK)
        return status;

    status = mx_vmar_map(mx_vmar_root_self(), 0, *phys_mem, 0, size, kMapFlags, addr);
    if (status != MX_OK) {
        mx_handle_close(*phys_mem);
        return status;
    }

    return MX_OK;
}

mx_status_t guest_create_page_table(uintptr_t addr, size_t size, uintptr_t* end_off) {
    if (size % PAGE_SIZE != 0)
        return MX_ERR_INVALID_ARGS;
    if (size > kMaxSize || size < kMinSize)
        return MX_ERR_OUT_OF_RANGE;

#if __x86_64__
    uint64_t aspace_off = 0;
    *end_off = 0;
    *end_off = page_table(addr, size - aspace_off, kPml4PageSize, *end_off, &aspace_off, false, 0);
    *end_off = page_table(addr, size - aspace_off, kPdpPageSize, *end_off, &aspace_off, true, X86_PTE_PS);
    *end_off = page_table(addr, size - aspace_off, kPdPageSize, *end_off, &aspace_off, true, X86_PTE_PS);
    *end_off = page_table(addr, size - aspace_off, kPtPageSize, *end_off, &aspace_off, true, 0);
    return MX_OK;
#else // __x86_64__
    return MX_ERR_NOT_SUPPORTED;
#endif // __x86_64__
}

static mx_status_t num_e820_entries(size_t size) {
    return size > kAddr4000mb ? 4 : 3;
}

mx_status_t guest_create_e820_memory_map(uintptr_t addr, size_t size, uintptr_t e820_off,
                                         int* num_entries) {
    size_t e820_size = num_e820_entries(size) * sizeof(e820entry_t);
    if (e820_off + e820_size > size) {
        return MX_ERR_BUFFER_TOO_SMALL;
    }

    e820entry_t* entry = (e820entry_t*)(addr + e820_off);
    memset(entry, 0, e820_size);
    // 0 to 1mb is reserved.
    entry[0].addr = 0;
    entry[0].size = kAddr1mb;
    entry[0].type = kE820Reserved;
    // 1mb to min(size, 3500mb) is available.
    entry[1].addr = kAddr1mb;
    entry[1].size = (size < kAddr3500mb ? size : kAddr3500mb) - kAddr1mb;
    entry[1].type = kE820Ram;
    // 3500mb to 4000mb is reserved.
    entry[2].addr = kAddr3500mb;
    entry[2].size = kAddr4000mb - kAddr3500mb;
    entry[2].type = kE820Reserved;
    if (size > kAddr4000mb) {
        // If size > 4000mb, then make that region available.
        entry[3].addr = kAddr4000mb;
        entry[3].size = size - kAddr4000mb;
        entry[3].type = kE820Ram;
        *num_entries = 4;
    } else {
        *num_entries = 3;
    }

    return MX_OK;
}

mx_status_t guest_create_bootdata(uintptr_t addr, size_t size, uintptr_t acpi_off,
                                  uintptr_t bootdata_off) {
    if (BOOTDATA_ALIGN(bootdata_off) != bootdata_off)
        return MX_ERR_INVALID_ARGS;

    size_t e820_size = num_e820_entries(size) * sizeof(e820entry_t);
    const uint32_t max_bootdata_len = sizeof(bootdata_t) + BOOTDATA_ALIGN(sizeof(uint64_t)) +
                                      sizeof(bootdata_t) + BOOTDATA_ALIGN(e820_size);
    if (bootdata_off + max_bootdata_len > size)
        return MX_ERR_BUFFER_TOO_SMALL;

    // Bootdata container.
    bootdata_t* header = (bootdata_t*)(addr + bootdata_off);
    header->type = BOOTDATA_CONTAINER;
    header->extra = BOOTDATA_MAGIC;
    header->length = max_bootdata_len;

    // ACPI root table pointer.
    bootdata_off += sizeof(bootdata_t);
    bootdata_t* bootdata = (bootdata_t*)(addr + bootdata_off);
    bootdata->type = BOOTDATA_ACPI_RSDP;
    bootdata->length = sizeof(uint64_t);

    bootdata_off += sizeof(bootdata_t);
    uint64_t* acpi_rsdp = (uint64_t*)(addr + bootdata_off);
    *acpi_rsdp = acpi_off;

    // E820 memory map.
    bootdata_off += BOOTDATA_ALIGN(sizeof(uint64_t));
    bootdata = (bootdata_t*)(addr + bootdata_off);
    bootdata->type = BOOTDATA_E820_TABLE;
    bootdata->length = e820_size;

    bootdata_off += sizeof(bootdata_t);
    int num_entries = 0;
    mx_status_t status = guest_create_e820_memory_map(addr, size, bootdata_off, &num_entries);
    if (status != MX_OK)
        return status;
    if (num_entries != num_e820_entries(size))
        return MX_ERR_BAD_STATE;

    return MX_OK;
}

mx_status_t guest_create(mx_handle_t hypervisor, mx_handle_t phys_mem, mx_handle_t* ctl_fifo,
                         mx_handle_t* guest) {
    const uint32_t count = PAGE_SIZE / MX_GUEST_MAX_PKT_SIZE;
    const uint32_t size = sizeof(mx_guest_packet_t);
    mx_handle_t kernel_ctl_fifo;
    mx_status_t status = mx_fifo_create(count, size, 0, &kernel_ctl_fifo, ctl_fifo);
    if (status != MX_OK)
        return status;

    mx_handle_t create_args[2] = { phys_mem, kernel_ctl_fifo };
    return mx_hypervisor_op(hypervisor, MX_HYPERVISOR_OP_GUEST_CREATE,
                            create_args, sizeof(create_args), guest, sizeof(*guest));
}
