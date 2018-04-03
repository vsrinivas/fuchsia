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

constexpr size_t kEntriesPerPinTxn = PAGE_SIZE / sizeof(zx_paddr_t);

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

Gtt::~Gtt() {
    if (scratch_buffer_paddr_) {
        scratch_buffer_pmt_.unpin();
    }
}

zx_status_t Gtt::Init(Controller* controller) {
    controller_ = controller;

    zx_status_t status = pci_get_bti(controller->pci(), 0, bti_.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to get bti: %d\n", status);
        return status;
    }

    zx_info_bti_t info;
    status = bti_.get_info(ZX_INFO_BTI, &info, sizeof(zx_info_bti_t), nullptr, nullptr);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: failed to fetch bti info %d\n", status);
        return status;
    }
    min_contiguity_ = info.minimum_contiguity;

    // Calculate the size of the gtt.
    auto gmch_gfx_ctrl = registers::GmchGfxControl::Get().FromValue(0);
    status = pci_config_read16(controller_->pci(), gmch_gfx_ctrl.kAddr,
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

    status = bti_.pin(ZX_BTI_PERM_READ, scratch_buffer_, 0, PAGE_SIZE, &scratch_buffer_paddr_, 1,
                      &scratch_buffer_pmt_);
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
    auto r = fbl::make_unique<GttRegion>(this);
    if (region_allocator_.GetRegion(region_length, align_pow2, r->region_) != ZX_OK) {
        return nullptr;
    }

    zx_paddr_t paddrs[kEntriesPerPinTxn];
    zx_status_t status;
    uint32_t num_pages = ROUNDUP(length, PAGE_SIZE) / PAGE_SIZE;
    uint64_t vmo_offset = 0;
    uint32_t pte_idx = static_cast<uint32_t>(r->base() / PAGE_SIZE);
    uint32_t pte_idx_end = pte_idx + num_pages;

    size_t num_pins = ROUNDUP(length, min_contiguity_) / min_contiguity_;
    fbl::AllocChecker ac;
    r->pmts_.reserve(num_pins, &ac);
    if (!ac.check()) {
        return nullptr;
    }

    while (pte_idx < pte_idx_end) {
        uint64_t cur_len = (pte_idx_end - pte_idx) * PAGE_SIZE;
        if (cur_len > kEntriesPerPinTxn * min_contiguity_) {
            cur_len = kEntriesPerPinTxn * min_contiguity_;
        }

        uint64_t actual_entries = ROUNDUP(cur_len, min_contiguity_) / min_contiguity_;
        zx::pmt pmt;
        status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_COMPRESS, *buffer,
                          vmo_offset, cur_len, paddrs, actual_entries, &pmt);
        if (status != ZX_OK) {
            zxlogf(ERROR, "i915: Failed to get paddrs (%d)\n", status);
            return nullptr;
        }
        vmo_offset += cur_len;
        r->mapped_end_ = static_cast<uint32_t>(vmo_offset);
        r->pmts_.push_back(fbl::move(pmt), &ac);
        ZX_DEBUG_ASSERT(ac.check()); // Shouldn't fail because of the reserve above.

        for (unsigned i = 0; i < actual_entries; i++) {
            for (unsigned j = 0; j < min_contiguity_ / PAGE_SIZE && pte_idx < pte_idx_end; j++) {
                uint64_t pte = gen_pte_encode(paddrs[i] + j * PAGE_SIZE, true);
                controller_->mmio_space()->Write<uint64_t>(get_pte_offset(pte_idx++), pte);
            }
        }
    }
    uint64_t padding_pte = gen_pte_encode(scratch_buffer_paddr_, true);
    for (unsigned i = 0; i < pte_padding; i++) {
        controller_->mmio_space()->Write<uint64_t>(get_pte_offset(pte_idx++), padding_pte);
    }
    controller_->mmio_space()->Read<uint32_t>(get_pte_offset(pte_idx - 1)); // Posting read

    return fbl::unique_ptr<const GttRegion>(r.release());
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

GttRegion::GttRegion(Gtt* gtt) : gtt_(gtt) {}

GttRegion::~GttRegion() {
    uint32_t pte_idx = static_cast<uint32_t>(region_->base / PAGE_SIZE);
    uint64_t pte = gen_pte_encode(gtt_->scratch_buffer_paddr_, false);
    auto mmio_space = gtt_->controller_->mmio_space();

    for (unsigned i = 0; i < mapped_end_ / PAGE_SIZE; i++) {
        uint32_t pte_offset = get_pte_offset(pte_idx++);
        mmio_space->Write<uint64_t>(pte_offset, pte);
    }

    mmio_space->Read<uint32_t>(get_pte_offset(pte_idx - 1)); // Posting read

    for (zx::pmt& pmt : pmts_) {
        if (pmt.unpin() != ZX_OK) {
             zxlogf(INFO, "Error unpinning gtt region\n");
        }
    }
}

} // namespace i915
