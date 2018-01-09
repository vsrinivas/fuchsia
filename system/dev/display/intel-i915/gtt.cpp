// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/pci.h>

#include <fbl/algorithm.h>
#include <fbl/limits.h>
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

Gtt::Gtt() :
    region_allocator_(RegionAllocator::RegionPool::Create(fbl::numeric_limits<size_t>::max())) {}

zx_status_t Gtt::Init(hwreg::RegisterIo* mmio_space, uint32_t gtt_size) {
    zxlogf(SPEW, "i915: Gtt::Init gtt_size (for page tables) 0x%x\n", gtt_size);

    uint64_t pte = gen_pte_encode(0, false);
    unsigned i;
    for (i = 0; i < gtt_size / sizeof(uint64_t); i++) {
        mmio_space->Write<uint64_t>(get_pte_offset(i), pte);
    }
    mmio_space->Read<uint32_t>(get_pte_offset(i)); // Posting read

    uint32_t gfx_mem_size = static_cast<uint32_t>(gtt_size / sizeof(uint64_t) * PAGE_SIZE);
    return region_allocator_.AddRegion({ .base = 0, .size = gfx_mem_size });
}

fbl::unique_ptr<const GttRegion> Gtt::Insert(hwreg::RegisterIo* mmio_space, zx::vmo* buffer,
                                             uint32_t length, uint32_t align_pow2,
                                             uint32_t pte_padding) {
    uint32_t region_length = ROUNDUP(length, PAGE_SIZE) + (pte_padding * PAGE_SIZE);
    fbl::unique_ptr<const GttRegion> r;
    if (region_allocator_.GetRegion(region_length, align_pow2, r) != ZX_OK) {
        return nullptr;
    }

    unsigned num_entries = PAGE_SIZE / sizeof(zx_paddr_t);
    zx_paddr_t paddrs[num_entries];
    unsigned i = 0;
    zx_status_t status;
    uint32_t pte_idx = static_cast<uint32_t>(r->base / PAGE_SIZE);

    while (i < length / PAGE_SIZE) {
        uint32_t cur_len = fbl::min(length - (i * PAGE_SIZE), num_entries * PAGE_SIZE);
        status = buffer->op_range(ZX_VMO_OP_LOOKUP, i * PAGE_SIZE, cur_len, paddrs, PAGE_SIZE);
        if (status != ZX_OK) {
            zxlogf(SPEW, "i915: Failed to get paddrs (%d)\n", status);
            return nullptr;
        }
        for (unsigned j = 0; j < num_entries && i < length / PAGE_SIZE; i++, j++) {
            uint64_t pte = gen_pte_encode(paddrs[j], true);
            mmio_space->Write<uint64_t>(get_pte_offset(pte_idx++), pte);
        }
    }
    uint64_t padding_pte = gen_pte_encode(paddrs[0], true);
    for (i = 0; i < pte_padding; i++) {
        mmio_space->Write<uint64_t>(get_pte_offset(pte_idx++), padding_pte);
    }
    mmio_space->Read<uint32_t>(get_pte_offset(pte_idx - 1)); // Posting read

    return r;
}

} // namespace i915
