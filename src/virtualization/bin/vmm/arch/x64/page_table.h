// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_PAGE_TABLE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_PAGE_TABLE_H_

class PhysMem;

/**
 * Create an identity-mapped page table.
 *
 * @param addr The mapped address of guest physical memory.
 * @param size The size of guest physical memory.
 * @param end_off The offset to the end of the page table.
 */
zx_status_t create_page_table(const PhysMem& phys_mem);

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_PAGE_TABLE_H_
