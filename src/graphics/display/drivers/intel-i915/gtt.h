// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_GTT_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_GTT_H_

#include <lib/zx/bti.h>
#include <lib/zx/vmo.h>

#include <memory>

#include <ddk/protocol/display/controller.h>
#include <fbl/vector.h>
#include <hwreg/mmio.h>
#include <region-alloc/region-alloc.h>

namespace i915 {

class Controller;
class Gtt;

class GttRegion {
 public:
  explicit GttRegion(Gtt* gtt);
  ~GttRegion();

  void SetRotation(uint32_t rotation, const image_t& image);

  zx_status_t PopulateRegion(zx_handle_t vmo, uint64_t page_offset, uint64_t length,
                             bool writable = false);
  void ClearRegion();

  uint64_t base() const { return region_->base; }
  uint64_t size() const { return region_->size; }

 private:
  RegionAllocator::Region::UPtr region_;
  Gtt* gtt_;

  fbl::Vector<zx::pmt> pmts_;
  uint32_t mapped_end_ = 0;
  // The region's current vmo. The region does not own the vmo handle; it
  // is up to the owner of the region to determine when the vmo should be
  // closed.
  zx_handle_t vmo_ = ZX_HANDLE_INVALID;

  bool is_rotated_;

  friend class Gtt;
};

class Gtt {
 public:
  Gtt();
  ~Gtt();
  zx_status_t Init(Controller* controller);
  zx_status_t AllocRegion(uint32_t length, uint32_t align_pow2,
                          std::unique_ptr<GttRegion>* region_out);
  void SetupForMexec(uintptr_t stolen_fb, uint32_t length);

  uint64_t size() const { return gfx_mem_size_; }

 private:
  Controller* controller_;

  uint64_t gfx_mem_size_;
  RegionAllocator region_allocator_;
  zx::vmo scratch_buffer_;
  zx::bti bti_;
  zx::pmt scratch_buffer_pmt_;
  zx_paddr_t scratch_buffer_paddr_ = 0;
  uint64_t min_contiguity_;

  friend class GttRegion;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Gtt);
};

}  // namespace i915

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_GTT_H_
