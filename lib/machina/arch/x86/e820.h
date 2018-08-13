// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_ARCH_X86_E820_H_
#define GARNET_LIB_MACHINA_ARCH_X86_E820_H_

#include "garnet/lib/machina/phys_mem.h"

namespace machina {

/**
 * Return the number of entries in the e820 memory map.
 *
 * @param size Size of guest physical memory.
 */
size_t e820_entries(size_t size);

/**
 * Create an e820 memory map.
 *
 * @param addr Address to place e820 memory map.
 * @param size Size of guest physical memory.
 */
void create_e820(void* addr, size_t size);

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_ARCH_X86_E820_H_
