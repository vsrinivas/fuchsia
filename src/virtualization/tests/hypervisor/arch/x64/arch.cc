// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/hypervisor/arch.h"

#include <fbl/span.h>

#include "src/virtualization/tests/hypervisor/hypervisor_tests.h"

enum {
  X86_PTE_P = 0x01,   // P    Valid
  X86_PTE_RW = 0x02,  // R/W  Read/Write
  X86_PTE_U = 0x04,   // U    Page is user accessible
  X86_PTE_PS = 0x80,  // PS   Page size
};

void SetUpGuestPageTable(fbl::Span<uint8_t> guest_memory) {
  ZX_ASSERT(guest_memory.size() >= 2ul * PAGE_SIZE);

  // Construct a page table consisting of two levels:
  //
  //   [0x0000, 0x1000)  // PML4
  //   [0x1000, 0x2000)  // PDP
  //
  // We map in the first 1 GiB of memory 1:1.

  // PML4 entry pointing to (addr + 0x1000)
  uint64_t* pte_off = reinterpret_cast<uint64_t*>(guest_memory.data());
  *pte_off = PAGE_SIZE | X86_PTE_P | X86_PTE_U | X86_PTE_RW;

  // PDP entry with 1GB page.
  pte_off = reinterpret_cast<uint64_t*>(guest_memory.data() + PAGE_SIZE);
  *pte_off = X86_PTE_PS | X86_PTE_P | X86_PTE_U | X86_PTE_RW;
}
