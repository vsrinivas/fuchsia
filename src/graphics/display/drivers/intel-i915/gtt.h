// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_GTT_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_GTT_H_

#include <fuchsia/hardware/display/controller/c/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <fbl/vector.h>
#include <hwreg/mmio.h>
#include <region-alloc/region-alloc.h>

namespace i915 {

// The offset into the MMIO space (at BAR 0) where the GTT is stored.
constexpr uint32_t GTT_BASE_OFFSET = 0x800000;

class Gtt;

class GttRegion {
 public:
  virtual ~GttRegion() = default;
  GttRegion(const GttRegion&) = delete;
  GttRegion& operator=(const GttRegion&) = delete;

  // This is the same as stride in units of bytes.
  virtual uint64_t bytes_per_row() const = 0;
  virtual uint64_t base() const = 0;

 protected:
  GttRegion() = default;
};

class GttRegionImpl : public GttRegion {
 public:
  GttRegionImpl(Gtt* gtt, RegionAllocator::Region::UPtr region);
  ~GttRegionImpl() override;

  void SetRotation(uint32_t rotation, const image_t& image);

  zx_status_t PopulateRegion(zx_handle_t vmo, uint64_t page_offset, uint64_t length,
                             bool writable = false);
  void ClearRegion();

  uint64_t base() const override { return region_->base; }
  uint64_t size() const { return region_->size; }
  uint64_t bytes_per_row() const override { return bytes_per_row_; }
  void set_bytes_per_row(uint64_t bytes_per_row) { bytes_per_row_ = bytes_per_row; }

 private:
  RegionAllocator::Region::UPtr region_;
  Gtt* gtt_;

  fbl::Vector<zx::pmt> pmts_;
  uint32_t mapped_end_ = 0;
  // The region's current vmo. The region does not own the vmo handle; it
  // is up to the owner of the region to determine when the vmo should be
  // closed.
  zx_handle_t vmo_ = ZX_HANDLE_INVALID;

  bool is_rotated_ = false;
  // Populated immediately after construction. Only valid for images (not arbitrary GTT regions).
  size_t bytes_per_row_ = 0;
};

class Gtt {
 public:
  Gtt();
  ~Gtt();

  // Initialize the GTT using the given parameters.
  //
  // |pci|: The PCI protocol implementation
  // |buffer|: The MMIO region that stores the GTT. The contents of the GTT must start at offset 0.
  // |fb_offset|: The offset to the end of the bootloader framebuffer in GTT-mapped memory.
  zx_status_t Init(const ddk::Pci& pci, fdf::MmioBuffer buffer, uint32_t fb_offset);
  zx_status_t AllocRegion(uint32_t length, uint32_t align_pow2,
                          std::unique_ptr<GttRegionImpl>* region_out);
  void SetupForMexec(uintptr_t stolen_fb, uint32_t length);

  uint64_t size() const { return gfx_mem_size_; }

 private:
  friend class GttRegionImpl;

  std::optional<fdf::MmioBuffer> buffer_;

  uint64_t gfx_mem_size_;
  RegionAllocator region_allocator_;
  zx::vmo scratch_buffer_;
  zx::bti bti_;
  zx::pmt scratch_buffer_pmt_;
  zx_paddr_t scratch_buffer_paddr_ = 0;
  uint64_t min_contiguity_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Gtt);
};

}  // namespace i915

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_GTT_H_
