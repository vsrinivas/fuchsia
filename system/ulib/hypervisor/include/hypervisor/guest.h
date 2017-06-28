// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

typedef struct e820entry {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} __PACKED e820entry_t;

/**
 * Create a VMO for guest physical memory.
 *
 * @param addr The mapped address of the VMO.
 * @param size The size of the VMO to create.
 * @param phys_mem The VMO representing the guest's physical memory.
 */
mx_status_t guest_create_phys_mem(uintptr_t* addr, size_t size, mx_handle_t* phys_mem);

/**
 * Create an identity-mapped page table.
 *
 * @param addr The mapped address of guest physical memory.
 * @param size The size of guest physical memory.
 * @param end_off The offset to the end of the page table.
 */
mx_status_t guest_create_page_table(uintptr_t addr, size_t size, uintptr_t* end_off);

/**
 * Return the size in bytes of e820 memory map.
 *
 * @param size The size of guest physical memory.
 */
size_t guest_e820_size(size_t size);

/**
 * Create an e820 memory map.
 *
 * @param addr The mapped address of guest physical memory.
 * @param size The size of guest physical memory.
 * @param e820_off The offset to the e820 memory map.
 */
mx_status_t guest_create_e820(uintptr_t addr, size_t size, uintptr_t e820_off);

/**
 * Create a guest with the given VMO containing its physical memory.
 *
 * @param hypervisor The hypervisor to launch the guest under.
 * @param phys_mem The VMO representing the guest's physical memory.
 * @param ctl_fifo A handle to the control FIFO for the guest.
 * @param guest A handle to the newly created guest.
 */
mx_status_t guest_create(mx_handle_t hypervisor, mx_handle_t phys_mem, mx_handle_t* ctl_fifo,
                         mx_handle_t* guest);

__END_CDECLS
