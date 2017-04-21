// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/types.h>

__BEGIN_CDECLS;

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
 * @param addr The mapped address where the page table will be created.
 * @param size The size of the memory to map.
 * @param pte_offset The offset of the end of the page table.
 */
mx_status_t guest_create_page_table(uintptr_t addr, size_t size, uintptr_t* pte_off);

/**
 * Create an ACPI 1.0 table.
 *
 * @param addr The mapped address of the guest physical memory.
 * @param size The size of guest physical memory.
 * @param pte_off The offset of the end of the page table. Used to ensure we
 *                don't collide with the page table.
 */
mx_status_t guest_create_acpi_table(uintptr_t addr, size_t size, uintptr_t pte_off);

/**
 * Create a guest with the given VMO containing its physical memory.
 *
 * @param hypervisor The hypervisor to launch the guest under.
 * @param hypervisor The VMO representing the guest's physical memory.
 * @param serial_fifo The FIFO where the guest's serial output will be written.
 * @param guest A handle to the newly created guest.
 */
mx_status_t guest_create(mx_handle_t hypervisor, mx_handle_t phys_mem, mx_handle_t* serial_fifo,
                         mx_handle_t* guest);

__END_CDECLS;
