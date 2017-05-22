// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

__BEGIN_CDECLS

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
 * Create an ACPI 1.0 table.
 *
 * @param addr The mapped address of guest physical memory.
 * @param size The size of guest physical memory.
 * @param acpi_off The offset to write the ACPI table.
 */
mx_status_t guest_create_acpi_table(uintptr_t addr, size_t size, uintptr_t acpi_off);

/**
 * Create bootdata for the guest.
 *
 * @param addr The mapped address of guest physical memory.
 * @param size The size of guest physical memory.
 * @param acpi_off The offset of the ACPI table.
 * @param bootdata_off The offset to write the bootdata.
 */
mx_status_t guest_create_bootdata(uintptr_t addr, size_t size, uintptr_t acpi_off,
                                  uintptr_t bootdata_off);

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
