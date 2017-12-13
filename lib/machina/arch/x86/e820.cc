// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/x86/e820.h"

#include "garnet/lib/machina/address.h"

static const uint32_t kE820Ram = 1;
static const uint32_t kE820Reserved = 2;

// clang-format off

static const uint64_t kAddr32kb     = 0x0000000000008000;
static const uint64_t kAddr64kb     = 0x0000000000010000;
static const uint64_t kAddr1mb      = 0x0000000000100000;
static const uint64_t kAddr3500mb   = 0x00000000e0000000;
static const uint64_t kAddr4000mb   = 0x0000000100000000;

// clang-format on

typedef struct e820entry {
  uint64_t addr;
  uint64_t size;
  uint32_t type;
} __PACKED e820entry_t;

namespace machina {

size_t e820_entries(size_t size) {
  return (size > kAddr4000mb ? 6 : 5);
}

size_t e820_size(size_t size) {
  return e820_entries(size) * sizeof(e820entry_t);
}

zx_status_t create_e820(uintptr_t addr, size_t size, uintptr_t e820_off) {
  if (e820_off + e820_size(size) > size)
    return ZX_ERR_BUFFER_TOO_SMALL;

  e820entry_t* entry = (e820entry_t*)(addr + e820_off);
  // 0 to 32kb is reserved.
  entry[0].addr = 0;
  entry[0].size = kAddr32kb;
  entry[0].type = kE820Reserved;
  // 32kb to to 64kb is available (for linux's real mode trampoline).
  entry[1].addr = kAddr32kb;
  entry[1].size = kAddr32kb;
  entry[1].type = kE820Ram;
  // 64kb to 1mb is reserved.
  entry[2].addr = kAddr64kb;
  entry[2].size = kAddr1mb - kAddr64kb;
  entry[2].type = kE820Reserved;
  // 1mb to min(size, 3500mb) is available.
  entry[3].addr = kAddr1mb;
  entry[3].size = (size < kAddr3500mb ? size : kAddr3500mb) - kAddr1mb;
  entry[3].type = kE820Ram;
  // 3500mb to 4000mb is reserved.
  entry[4].addr = kAddr3500mb;
  entry[4].size = kAddr4000mb - kAddr3500mb;
  entry[4].type = kE820Reserved;
  if (size > kAddr4000mb) {
    // If size > 4000mb, then make that region available.
    entry[5].addr = kAddr4000mb;
    entry[5].size = size - kAddr4000mb;
    entry[5].type = kE820Ram;
  }

  return ZX_OK;
}

}  // namespace machina
