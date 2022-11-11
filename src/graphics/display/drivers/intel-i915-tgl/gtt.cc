// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/gtt.h"

#include <lib/ddk/debug.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/align.h>
#include <stdlib.h>

#include <climits>
#include <limits>
#include <memory>
#include <utility>

#include <fbl/algorithm.h>

#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"
#include "src/graphics/display/drivers/intel-i915-tgl/tiling.h"

#define PAGE_PRESENT (1 << 0)

namespace {

constexpr size_t kEntriesPerPinTxn = PAGE_SIZE / sizeof(zx_paddr_t);

inline uint64_t gen_pte_encode(uint64_t bus_addr) {
  // Make every page present so we don't have to deal with padding for framebuffers
  return bus_addr | PAGE_PRESENT;
}

inline uint32_t get_pte_offset(uint32_t idx) {
  return static_cast<uint32_t>(idx * sizeof(uint64_t));
}

}  // namespace

namespace i915_tgl {

Gtt::Gtt()
    : region_allocator_(RegionAllocator::RegionPool::Create(std::numeric_limits<size_t>::max())) {}

Gtt::~Gtt() {
  if (scratch_buffer_paddr_) {
    scratch_buffer_pmt_.unpin();
  }
}

zx_status_t Gtt::Init(const ddk::Pci& pci, fdf::MmioBuffer buffer, uint32_t fb_offset) {
  ZX_DEBUG_ASSERT(pci.is_valid());
  buffer_ = std::move(buffer);

  zx_status_t status = pci.GetBti(0, &bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to get bti (%d)", status);
    return status;
  }

  zx_info_bti_t info;
  status = bti_.get_info(ZX_INFO_BTI, &info, sizeof(zx_info_bti_t), nullptr, nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to fetch bti info (%d)", status);
    return status;
  }
  min_contiguity_ = info.minimum_contiguity;

  // Calculate the size of the gtt.
  auto gmch_gfx_ctrl = tgl_registers::GmchGfxControl::Get().FromValue(0);
  status = pci.ReadConfig16(gmch_gfx_ctrl.kAddr, gmch_gfx_ctrl.reg_value_ptr());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read GfxControl");
    return status;
  }
  uint32_t gtt_size = gmch_gfx_ctrl.gtt_mappable_mem_size();
  zxlogf(TRACE, "Gtt::Init gtt_size (for page tables) 0x%x", gtt_size);
  if (gtt_size == 0) {
    // IHD-OS-KBL-Vol 5-1.17 (intel-gfx-prm-osrc-kbl-vol05-memory_views.pdf p.35) lists that the GPU
    // supports a global GTT and the size can be either 128KB, 256KB, or 512KB, which further map to
    // aperture sizes of 128MB, 256MB, and 512MB). Here we are treating a 0-size aperture as
    // illegal.
    //
    // TODO(armansito): The "GMCH Graphics Control" (GGC_0_0_0_PCI) register documentation says that
    // the |gtt_size| value here actually corresponds to "the amount of main memory that is
    // pre-allocated to supported the Internal GTT", which comes in sizes of 2MB, 4MB, and 8MB. Is
    // it an error if the BIOS does not pre-allocate this memory?
    zxlogf(ERROR, "The BIOS pre-allocated memory size for the internal GTT is 0! Aborting.");
    return ZX_ERR_INTERNAL;
  }

  status = zx::vmo::create(PAGE_SIZE, 0, &scratch_buffer_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to alloc scratch buffer (%d)", status);
    return status;
  }

  status = bti_.pin(ZX_BTI_PERM_READ, scratch_buffer_, 0, PAGE_SIZE, &scratch_buffer_paddr_, 1,
                    &scratch_buffer_pmt_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to look up scratch buffer (%d)", status);
    return status;
  }

  scratch_buffer_.op_range(ZX_VMO_OP_CACHE_CLEAN, 0, PAGE_SIZE, nullptr, 0);

  // Populate the gtt with the scratch buffer. If we've been given an offset for the bootloader
  // framebuffer, then leave the range up to |fb_offset| unchanged as the bootloader framebuffer
  // gets allocated out of stolen memory.
  uint32_t offset = ZX_ROUNDUP(fb_offset, PAGE_SIZE);
  uint64_t pte = gen_pte_encode(scratch_buffer_paddr_);
  unsigned i;
  for (i = offset / PAGE_SIZE; i < gtt_size / sizeof(uint64_t); i++) {
    buffer_->Write<uint64_t>(pte, get_pte_offset(i));
  }
  buffer_->Read<uint32_t>(get_pte_offset(i - 1));  // Posting read

  gfx_mem_size_ = gtt_size / sizeof(uint64_t) * PAGE_SIZE;
  return region_allocator_.AddRegion({.base = offset, .size = gfx_mem_size_ - offset});
}

zx_status_t Gtt::AllocRegion(uint32_t length, uint32_t align_pow2,
                             std::unique_ptr<GttRegionImpl>* region_out) {
  uint32_t region_length = ZX_ROUNDUP(length, PAGE_SIZE);
  RegionAllocator::Region::UPtr region;
  if (region_allocator_.GetRegion(region_length, align_pow2, region) != ZX_OK) {
    return ZX_ERR_NO_RESOURCES;
  }

  fbl::AllocChecker ac;
  auto r = fbl::make_unique_checked<GttRegionImpl>(&ac, this, std::move(region));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  *region_out = std::move(r);
  return ZX_OK;
}

void Gtt::SetupForMexec(uintptr_t stolen_fb, uint32_t length) {
  // Just clobber everything to get the bootloader framebuffer to work.
  unsigned pte_idx = 0;
  for (unsigned i = 0; i < ZX_ROUNDUP(length, PAGE_SIZE) / PAGE_SIZE; i++, stolen_fb += PAGE_SIZE) {
    uint64_t pte = gen_pte_encode(stolen_fb);
    buffer_->Write<uint64_t>(pte, get_pte_offset(pte_idx++));
  }
  buffer_->Read<uint32_t>(get_pte_offset(pte_idx - 1));  // Posting read
}

GttRegionImpl::GttRegionImpl(Gtt* gtt, RegionAllocator::Region::UPtr region)
    : region_(std::move(region)), gtt_(gtt) {}
GttRegionImpl::~GttRegionImpl() { ClearRegion(); }

zx_status_t GttRegionImpl::PopulateRegion(zx_handle_t vmo, uint64_t page_offset, uint64_t length,
                                          bool writable) {
  if (length > region_->size) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (mapped_end_ != 0) {
    return ZX_ERR_ALREADY_BOUND;
  }
  vmo_ = vmo;

  zx_paddr_t paddrs[kEntriesPerPinTxn];
  zx_status_t status;
  uint32_t num_pages = static_cast<uint32_t>(ZX_ROUNDUP(length, PAGE_SIZE) / PAGE_SIZE);
  uint64_t vmo_offset = page_offset * PAGE_SIZE;
  uint32_t pte_idx = static_cast<uint32_t>(region_->base / PAGE_SIZE);
  uint32_t pte_idx_end = pte_idx + num_pages;

  size_t num_pins = ZX_ROUNDUP(length, gtt_->min_contiguity_) / gtt_->min_contiguity_;
  fbl::AllocChecker ac;
  pmts_.reserve(num_pins, &ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  int32_t flags = ZX_BTI_COMPRESS | ZX_BTI_PERM_READ | (writable ? ZX_BTI_PERM_WRITE : 0);
  while (pte_idx < pte_idx_end) {
    uint64_t cur_len = (pte_idx_end - pte_idx) * PAGE_SIZE;
    if (cur_len > kEntriesPerPinTxn * gtt_->min_contiguity_) {
      cur_len = kEntriesPerPinTxn * gtt_->min_contiguity_;
    }

    uint64_t actual_entries = ZX_ROUNDUP(cur_len, gtt_->min_contiguity_) / gtt_->min_contiguity_;
    zx::pmt pmt;
    status = gtt_->bti_.pin(flags, *zx::unowned_vmo(vmo_), vmo_offset, cur_len, paddrs,
                            actual_entries, &pmt);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to get paddrs (%d)", status);
      return status;
    }
    vmo_offset += cur_len;
    mapped_end_ = static_cast<uint32_t>(vmo_offset);
    pmts_.push_back(std::move(pmt), &ac);
    ZX_DEBUG_ASSERT(ac.check());  // Shouldn't fail because of the reserve above.

    for (unsigned i = 0; i < actual_entries; i++) {
      for (unsigned j = 0; j < gtt_->min_contiguity_ / PAGE_SIZE && pte_idx < pte_idx_end; j++) {
        uint64_t pte = gen_pte_encode(paddrs[i] + j * PAGE_SIZE);
        gtt_->buffer_->Write<uint64_t>(pte, get_pte_offset(pte_idx++));
      }
    }
  }

  gtt_->buffer_->Read<uint32_t>(get_pte_offset(pte_idx - 1));  // Posting read
  return ZX_OK;
}

void GttRegionImpl::ClearRegion() {
  if (!region_) {
    return;
  }

  uint32_t pte_idx = static_cast<uint32_t>(region_->base / PAGE_SIZE);
  uint64_t pte = gen_pte_encode(gtt_->scratch_buffer_paddr_);
  auto mmio_space = &gtt_->buffer_.value();

  for (unsigned i = 0; i < mapped_end_ / PAGE_SIZE; i++) {
    uint32_t pte_offset = get_pte_offset(pte_idx++);
    mmio_space->Write<uint64_t>(pte, pte_offset);
  }

  if (mapped_end_) {
    mmio_space->Read<uint32_t>(get_pte_offset(pte_idx - 1));  // Posting read
  }

  for (zx::pmt& pmt : pmts_) {
    if (pmt.unpin() != ZX_OK) {
      zxlogf(INFO, "Error unpinning gtt region");
    }
  }
  pmts_.reset();
  mapped_end_ = 0;

  if (vmo_ != ZX_HANDLE_INVALID) {
    zx_handle_close(vmo_);
  }
  vmo_ = ZX_HANDLE_INVALID;
}

void GttRegionImpl::SetRotation(uint32_t rotation, const image_t& image) {
  bool rotated = (rotation == FRAME_TRANSFORM_ROT_90 || rotation == FRAME_TRANSFORM_ROT_270);
  if (rotated == is_rotated_) {
    return;
  }
  is_rotated_ = rotated;
  // Displaying an image with 90/270 degree rotation requires rearranging the image's
  // GTT mapping. Since permutations are composed of disjoint cycles and because we can
  // calculate each page's location in the new mapping, we can remap the image by shifting
  // the GTT entries around each cycle. We use one of the ignored bits in the global GTT
  // PTEs to keep track of whether or not entries have been rotated.
  constexpr uint32_t kRotatedFlag = (1 << 1);

  uint64_t mask = is_rotated_ ? kRotatedFlag : 0;
  uint32_t width = bytes_per_row() / get_tile_byte_width(image.type, image.pixel_format);
  uint32_t height = height_in_tiles(image.type, image.height, image.pixel_format);

  auto mmio_space = &gtt_->buffer_.value();
  uint32_t pte_offset = static_cast<uint32_t>(base() / PAGE_SIZE);
  for (uint32_t i = 0; i < size() / PAGE_SIZE; i++) {
    uint64_t entry = mmio_space->Read<uint64_t>(get_pte_offset(i + pte_offset));
    uint32_t position = i;
    // If the entry has already been cycled into the correct place, the
    // loop check will immediately fail.
    while ((entry & kRotatedFlag) != mask) {
      if (mask) {
        uint32_t x = position % width;
        uint32_t y = position / width;
        position = ((x + 1) * height) - y - 1;
      } else {
        uint32_t x = position % height;
        uint32_t y = position / height;
        position = ((height - x - 1) * width) + y;
      }
      uint32_t dest_offset = get_pte_offset(position + pte_offset);

      uint64_t next_entry = mmio_space->Read<uint64_t>(dest_offset);
      mmio_space->Write<uint64_t>(entry ^ kRotatedFlag, dest_offset);
      entry = next_entry;
    }
  }
}

}  // namespace i915_tgl
