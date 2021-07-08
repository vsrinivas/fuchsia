// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/memory.h"

#include <fuchsia/virtualization/cpp/fidl.h>
#include <zircon/boot/image.h>

std::vector<zbi_mem_range_t> ZbiMemoryRanges(
    const std::vector<fuchsia::virtualization::MemorySpec>& specs, size_t mem_size,
    const DevMem& dev_mem) {
  std::vector<zbi_mem_range_t> ranges;
  auto yield = [&](zx_gpaddr_t addr, size_t size) {
    ranges.emplace_back(zbi_mem_range_t{
        .paddr = addr,
        .length = size,
        .type = ZBI_MEM_RANGE_RAM,
    });
  };

  for (const fuchsia::virtualization::MemorySpec& spec : specs) {
    // Do not use device memory when yielding normal memory.
    if (spec.policy != fuchsia::virtualization::MemoryPolicy::HOST_DEVICE) {
      dev_mem.YieldInverseRange(spec.base, spec.size, yield);
    }
  }

  // Zircon only supports a limited number of peripheral ranges so for any
  // dev_mem ranges that are not in the RAM range we will build a single
  // peripheral range to cover all of them.
  zbi_mem_range_t periph_range = {.paddr = 0, .length = 0, .type = ZBI_MEM_RANGE_PERIPHERAL};
  for (const auto& range : dev_mem) {
    if (range.addr < mem_size) {
      ranges.emplace_back(zbi_mem_range_t{
          .paddr = range.addr,
          .length = range.size,
          .type = ZBI_MEM_RANGE_PERIPHERAL,
      });
    } else {
      if (periph_range.length == 0) {
        periph_range.paddr = range.addr;
      }
      periph_range.length = range.addr + range.size - periph_range.paddr;
    }
  }
  if (periph_range.length != 0) {
    ranges.emplace_back(periph_range);
  }
  return ranges;
}
