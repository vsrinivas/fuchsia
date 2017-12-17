// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#ifndef GARNET_LIB_MACHINA_ARCH_X86_PAGE_TABLE_H_
#define GARNET_LIB_MACHINA_ARCH_X86_PAGE_TABLE_H_

namespace machina {

/**
 * Create an identity-mapped page table.
 *
 * @param addr The mapped address of guest physical memory.
 * @param size The size of guest physical memory.
 * @param end_off The offset to the end of the page table.
 */
zx_status_t create_page_table(uintptr_t addr, size_t size, uintptr_t* end_off);

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_X86_PAGE_TABLE_H_
