// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ARCH_ARM64_CONSTANTS_H_
#define SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ARCH_ARM64_CONSTANTS_H_

// Entry point for the guest, and where code will be written to in the guest's
// physical address space.
#define GUEST_ENTRY 0x0

// Location of the top-level page table, and maximum size of all page table
// data structures.
#define PAGE_TABLE_PADDR 0x10000
#define PAGE_TABLE_SIZE 0x10000

// Space reserved for the guest to read/write.
#define GUEST_SCRATCH_ADDR 0x20000

// Virtual memory region size.
#define REGION_SIZE_BITS 34

#endif  // SRC_VIRTUALIZATION_TESTS_HYPERVISOR_ARCH_ARM64_CONSTANTS_H_
