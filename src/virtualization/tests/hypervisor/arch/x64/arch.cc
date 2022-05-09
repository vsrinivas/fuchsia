// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/hypervisor/arch.h"

#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <page_tables/x86/constants.h>

void SetUpGuestPageTable(cpp20::span<uint8_t> guest_memory) {
  ZX_ASSERT(guest_memory.size() >= GUEST_ENTRY);

  // Construct a page table consisting of two levels:
  //
  //   [0x0000, 0x1000)  // PML4
  //   [0x1000, 0x2000)  // PDP
  //
  // We map in the first 1 GiB of memory 1:1.

  // PML4 entry pointing to (addr + 0x1000)
  uint64_t* pte_off = reinterpret_cast<uint64_t*>(guest_memory.data());
  *pte_off = PAGE_SIZE | X86_MMU_PG_P | X86_MMU_PG_U | X86_MMU_PG_RW;

  // PDP entry with 1GB page.
  pte_off = reinterpret_cast<uint64_t*>(guest_memory.data() + PAGE_SIZE);
  *pte_off = X86_MMU_PG_PS | X86_MMU_PG_P | X86_MMU_PG_U | X86_MMU_PG_RW;
}
