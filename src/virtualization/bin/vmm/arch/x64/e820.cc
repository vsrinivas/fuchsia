// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/e820.h"

// clang-format off

static constexpr uint64_t kAddr32kb     = 0x0000000000008000;
static constexpr uint64_t kAddr512kb    = 0x0000000000080000;
static constexpr uint64_t kAddr1mb      = 0x0000000000100000;

// clang-format on

E820Map::E820Map(size_t mem_size, const DevMem &dev_mem) {
  // 0 to 32kb is reserved.
  entries.emplace_back(e820entry_t{0, kAddr32kb, E820_RESERVED});
  // 32kb to to 512kb is available (for Linux's real mode trampoline).
  entries.emplace_back(
      e820entry_t{kAddr32kb, kAddr512kb - kAddr32kb, E820_RAM});
  // 512kb to 1mb is reserved.
  entries.emplace_back(
      e820entry_t{kAddr512kb, kAddr1mb - kAddr512kb, E820_RESERVED});
  // 1mb to min(size, 1mb) is available.
  dev_mem.YieldInverseRange(
      kAddr1mb, mem_size - kAddr1mb, [this](zx_gpaddr_t addr, size_t size) {
        entries.emplace_back(e820entry_t{addr, size, E820_RAM});
      });
}
