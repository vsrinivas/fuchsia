// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_X86_E820_H_
#define GARNET_LIB_MACHINA_ARCH_X86_E820_H_

#include "garnet/lib/machina/phys_mem.h"

namespace machina {

/**
 * Return the size in bytes of e820 memory map.
 *
 * @param size The size of guest physical memory.
 */
size_t e820_size(size_t size);

/**
 * Return the number of entries in the e820 memory map.
 *
 * @param size The size of guest physical memory.
 */
size_t e820_entries(size_t size);

/**
 * Create an e820 memory map.
 *
 * @param phys_mem The guest physical memory.
 * @param e820_off The offset to the e820 memory map.
 */
zx_status_t create_e820(const machina::PhysMem& phys_mem, uintptr_t e820_off);

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_X86_E820_H_
