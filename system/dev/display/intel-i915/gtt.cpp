// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/pci.h>

#include <fbl/algorithm.h>
#include <fbl/limits.h>
#include <limits.h>
#include <stdlib.h>

#include "intel-i915.h"
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

zx_status_t Gtt::Init(Controller* controller) {
    controller_ = controller;
    // Calculate the size of the gtt.
    auto gmch_gfx_ctrl = registers::GmchGfxControl::Get().FromValue(0);
    zx_status_t status = pci_config_read16(controller_->pci(), gmch_gfx_ctrl.kAddr,
                                           gmch_gfx_ctrl.reg_value_ptr());
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to read GfxControl\n");
        return status;
    }
    uint32_t gtt_size = gmch_gfx_ctrl.gtt_mappable_mem_size();
    zxlogf(SPEW, "i915: Gtt::Init gtt_size (for page tables) 0x%x\n", gtt_size);

    status = zx::vmo::create(PAGE_SIZE, 0, &scratch_buffer_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to alloc scratch buffer %d\n", status);
        return status;
    }
    status = scratch_buffer_.op_range(ZX_VMO_OP_COMMIT, 0, PAGE_SIZE, nullptr, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to commit scratch buffer %d\n", status);
        return status;
    }
    status = scratch_buffer_.op_range(ZX_VMO_OP_LOOKUP, 0, PAGE_SIZE,
                                      &scratch_buffer_paddr_, sizeof(zx_paddr_t));
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to look up scratch buffer %d\n", status);
        return status;
    }

    // Populate the gtt with the scratch buffer.
    uint64_t pte = gen_pte_encode(scratch_buffer_paddr_, false);
    unsigned i;
    for (i = 0; i < gtt_size / sizeof(uint64_t); i++) {
        controller_->mmio_space()->Write<uint64_t>(get_pte_offset(i), pte);
    }
    controller_->mmio_space()->Read<uint32_t>(get_pte_offset(i - i)); // Posting read

    size_t gfx_mem_size = gtt_size / sizeof(uint64_t) * PAGE_SIZE;
    return region_allocator_.AddRegion({ .base = 0, .size = gfx_mem_size });
}

fbl::unique_ptr<const GttRegion> Gtt::Insert(zx::vmo* buffer,
                                             uint32_t length, uint32_t align_pow2,
                                             uint32_t pte_padding) {
    uint32_t region_length = ROUNDUP(length, PAGE_SIZE) + (pte_padding * PAGE_SIZE);
    fbl::unique_ptr<const RegionAllocator::Region> r;
    if (region_allocator_.GetRegion(region_length, align_pow2, r) != ZX_OK) {
        return nullptr;
    }

    unsigned num_entries = PAGE_SIZE / sizeof(zx_paddr_t);
    zx_paddr_t paddrs[num_entries];
    unsigned i = 0;
    zx_status_t status;
    uint32_t pte_idx = static_cast<uint32_t>(r->base / PAGE_SIZE);

    uint32_t num_pages = ROUNDUP(length, PAGE_SIZE) / PAGE_SIZE;
    while (i < num_pages) {
        uint32_t cur_len = fbl::min(length - (i * PAGE_SIZE), num_entries * PAGE_SIZE);
        status = buffer->op_range(ZX_VMO_OP_LOOKUP, i * PAGE_SIZE, cur_len, paddrs, PAGE_SIZE);
        if (status != ZX_OK) {
            zxlogf(SPEW, "i915: Failed to get paddrs (%d)\n", status);
            return nullptr;
        }
        for (unsigned j = 0; j < num_entries && i < num_pages; i++, j++) {
            uint64_t pte = gen_pte_encode(paddrs[j], true);
            controller_->mmio_space()->Write<uint64_t>(get_pte_offset(pte_idx++), pte);
        }
    }
    uint64_t padding_pte = gen_pte_encode(scratch_buffer_paddr_, true);
    for (i = 0; i < pte_padding; i++) {
        controller_->mmio_space()->Write<uint64_t>(get_pte_offset(pte_idx++), padding_pte);
    }
    controller_->mmio_space()->Read<uint32_t>(get_pte_offset(pte_idx - 1)); // Posting read

    return fbl::make_unique<const GttRegion>(fbl::move(r), this);
}

void Gtt::SetupForMexec(uintptr_t stolen_fb, uint32_t length, uint32_t pte_padding) {
    // Just clobber everything to get the bootloader framebuffer to work.
    unsigned pte_idx = 0;
    for (unsigned i = 0; i < ROUNDUP(length, PAGE_SIZE) / PAGE_SIZE; i++, stolen_fb += PAGE_SIZE) {
        uint64_t pte = gen_pte_encode(stolen_fb, true);
        controller_->mmio_space()->Write<uint64_t>(get_pte_offset(pte_idx++), pte);
    }
    uint64_t padding_pte = gen_pte_encode(scratch_buffer_paddr_, true);
    for (unsigned i = 0; i < pte_padding; i++) {
        controller_->mmio_space()->Write<uint64_t>(get_pte_offset(pte_idx++), padding_pte);
    }
    controller_->mmio_space()->Read<uint32_t>(get_pte_offset(pte_idx - 1)); // Posting read
}

GttRegion::GttRegion(fbl::unique_ptr<const RegionAllocator::Region> region, Gtt* gtt)
        : region_(fbl::move(region)), gtt_(gtt) {}

GttRegion::~GttRegion() {
    uint32_t pte_idx = static_cast<uint32_t>(region_->base / PAGE_SIZE);
    uint64_t pte = gen_pte_encode(gtt_->scratch_buffer_paddr_, false);
    for (unsigned i = 0; i < region_->size / PAGE_SIZE; i++) {
        gtt_->controller_->mmio_space()->Write<uint64_t>(get_pte_offset(pte_idx++), pte);
    }

    gtt_->controller_->mmio_space()->Read<uint32_t>(get_pte_offset(pte_idx - 1)); // Posting read
}

} // namespace i915
