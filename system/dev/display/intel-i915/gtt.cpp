// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/pci.h>

#include <fbl/algorithm.h>
#include <limits.h>
#include <stdlib.h>

#include "gtt.h"
#include "registers.h"

#define PAGE_PRESENT (1 << 0)

namespace {

inline uint64_t gen_pte_encode(uint64_t bus_addr, bool valid)
{
    return bus_addr | (valid ? PAGE_PRESENT : 0);
}

inline uint32_t get_pte_offset(uint32_t idx) {
    constexpr uint32_t GTT_BASE_OFFSET = 0x800000;
    return static_cast<uint32_t>(GTT_BASE_OFFSET + (idx * sizeof(uint64_t)));
}

}

namespace i915 {

void Gtt::Init(MmioSpace* mmio_space, uint32_t gtt_size) {
    zxlogf(SPEW, "i915: Gtt::Init gtt_size (for page tables) 0x%x\n", gtt_size);

    uint64_t pte = gen_pte_encode(0, false);
    unsigned i;
    for (i = 0; i < gtt_size / sizeof(uint64_t); i++) {
        mmio_space->Write64(get_pte_offset(i), pte);
    }
    mmio_space->Read32(get_pte_offset(i)); // Posting read
}

bool Gtt::Insert(MmioSpace* mmio_space, zx::vmo* buffer,
                 uint32_t length, uint32_t pte_padding, uint32_t* gm_addr_out) {
    // TODO(ZX-1413): Do actual allocation management
    uint32_t gfx_addr = 0;

    unsigned num_entries = PAGE_SIZE / sizeof(zx_paddr_t);
    zx_paddr_t paddrs[num_entries];
    unsigned i = 0;
    zx_status_t status;
    uint32_t pte_idx = gfx_addr / PAGE_SIZE;

    while (i < length / PAGE_SIZE) {
        uint32_t cur_len = fbl::min(length - (i * PAGE_SIZE), num_entries * PAGE_SIZE);
        status = buffer->op_range(ZX_VMO_OP_LOOKUP, i * PAGE_SIZE, cur_len, paddrs, PAGE_SIZE);
        if (status != ZX_OK) {
            zxlogf(SPEW, "i915: Failed to get paddrs (%d)\n", status);
            return false;
        }
        for (unsigned j = 0; j < num_entries && i < length / PAGE_SIZE; i++, j++) {
            uint64_t pte = gen_pte_encode(paddrs[j], true);
            mmio_space->Write64(get_pte_offset(pte_idx++), pte);
        }
    }
    uint64_t padding_pte = gen_pte_encode(paddrs[0], true);
    for (i = 0; i < pte_padding; i++) {
        mmio_space->Write64(get_pte_offset(pte_idx++), padding_pte);
    }
    mmio_space->Read32(get_pte_offset(pte_idx - 1)); // Posting read

    *gm_addr_out = gfx_addr;

    return true;
}

} // namespace i915
