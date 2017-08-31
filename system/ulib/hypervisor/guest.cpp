// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <hypervisor/guest.h>
#include <magenta/device/sysinfo.h>

static const char kResourcePath[] = "/dev/misc/sysinfo";

static const uint32_t kE820Ram = 1;
static const uint32_t kE820Reserved = 2;

static const size_t kMaxSize = 512ull << 30;
static const size_t kMinSize = 4 * (4 << 10);

// clang-format off

static const uint64_t kAddr32kb     = 0x0000000000008000;
static const uint64_t kAddr64kb     = 0x0000000000010000;
static const uint64_t kAddr1mb      = 0x0000000000100000;
static const uint64_t kAddr3500mb   = 0x00000000e0000000;
static const uint64_t kAddr4000mb   = 0x0000000100000000;

#if __x86_64__
enum {
    X86_PTE_P   = 0x01, /* P    Valid           */
    X86_PTE_RW  = 0x02, /* R/W  Read/Write      */
    X86_PTE_PS  = 0x80, /* PS   Page size       */
};

// clang-format on

static const size_t kPml4PageSize = 512ull << 30;
static const size_t kPdpPageSize = 1 << 30;
static const size_t kPdPageSize = 2 << 20;
static const size_t kPtPageSize = 4 << 10;
static const size_t kPtesPerPage = PAGE_SIZE / sizeof(uint64_t);

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

size_t guest_e820_size(size_t size) {
    return (size > kAddr4000mb ? 6 : 5) * sizeof(e820entry_t);
}

mx_status_t guest_create_e820(uintptr_t addr, size_t size, uintptr_t e820_off) {
    if (e820_off + guest_e820_size(size) > size)
        return MX_ERR_BUFFER_TOO_SMALL;

    e820entry_t* entry = (e820entry_t*)(addr + e820_off);
    // 0 to 32kb is reserved.
    entry[0].addr = 0;
    entry[0].size = kAddr32kb;
    entry[0].type = kE820Reserved;
    // 32kb to to 64kb is available (for linux's real mode trampoline).
    entry[1].addr = kAddr32kb;
    entry[1].size = kAddr32kb;
    entry[1].type = kE820Ram;
    // 64kb to 1mb is reserved.
    entry[2].addr = kAddr64kb;
    entry[2].size = kAddr1mb - kAddr64kb;
    entry[2].type = kE820Reserved;
    // 1mb to min(size, 3500mb) is available.
    entry[3].addr = kAddr1mb;
    entry[3].size = (size < kAddr3500mb ? size : kAddr3500mb) - kAddr1mb;
    entry[3].type = kE820Ram;
    // 3500mb to 4000mb is reserved.
    entry[4].addr = kAddr3500mb;
    entry[4].size = kAddr4000mb - kAddr3500mb;
    entry[4].type = kE820Reserved;
    if (size > kAddr4000mb) {
        // If size > 4000mb, then make that region available.
        entry[5].addr = kAddr4000mb;
        entry[5].size = size - kAddr4000mb;
        entry[5].type = kE820Ram;
    }

    return MX_OK;
}

mx_status_t guest_get_resource(mx_handle_t* resource) {
    int fd = open(kResourcePath, O_RDWR);
    if (fd < 0)
        return MX_ERR_IO;
    ssize_t n = ioctl_sysinfo_get_hypervisor_resource(fd, resource);
    close(fd);
    return n < 0 ? MX_ERR_IO : MX_OK;
}
