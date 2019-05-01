// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_E820_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_E820_H_

#include <zircon/boot/e820.h>

#include <vector>

#include "src/virtualization/bin/vmm/dev_mem.h"

/**
 * Used to construct an E820 memory map
 *
 * It is not the responsibility of this class to detect or prevent region
 * overlap of either same or differently typed regions.
 */
class E820Map {
 public:
  /*
   * Create a new E820 map
   *
   * @param pmem_size Size of physical memory. The E820 map will contain as much
   *  RAM regions as can fit in the defined physical memory that do not
   *  collide with the provided dev_mem regions.
   */
  E820Map(size_t mem_size, const DevMem &dev_mem);

  void AddReservedRegion(zx_gpaddr_t addr, size_t size) {
    entries.emplace_back(e820entry_t{addr, size, E820_RESERVED});
  }

  size_t size() const { return entries.size(); }

  void copy(e820entry_t *dest) {
    std::copy(entries.begin(), entries.end(), dest);
  }

 private:
  std::vector<e820entry_t> entries;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_E820_H_
