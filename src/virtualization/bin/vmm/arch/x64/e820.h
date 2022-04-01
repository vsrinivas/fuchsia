// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_E820_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_E820_H_

#include <lib/zircon-internal/e820.h>

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
   * @param dev_mem Reserved device memory. Guest memory has already been selected to not
   *  overlap with these regions.
   *
   * @param guest_mem Guest memory regions.
   */
  E820Map(const DevMem& dev_mem, const std::vector<GuestMemoryRegion>& guest_mem);

  void AddReservedRegion(zx_gpaddr_t addr, size_t size) {
    entries_.emplace_back(E820Entry{addr, size, E820Type::kReserved});
  }

  size_t size() const { return entries_.size(); }

  void copy(cpp20::span<E820Entry> dest) {
    std::copy(entries_.begin(), entries_.end(), dest.begin());
  }

 private:
  std::vector<E820Entry> entries_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_E820_H_
