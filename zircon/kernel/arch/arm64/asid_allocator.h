// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_ARCH_ARM64_ASID_ALLOCATOR_H_
#define ZIRCON_KERNEL_ARCH_ARM64_ASID_ALLOCATOR_H_

#include <lib/zx/result.h>
#include <zircon/types.h>

#include <arch/arm64/feature.h>
#include <arch/arm64/mmu.h>
#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/macros.h>
#include <kernel/mutex.h>

// Class to automate allocating an ASID for a new address space for arm64.
//
// NOTE: stores as much space for 16bit ASIDs, but will fall back to limiting to 8 bit ASIDs
// given hardware support.
class AsidAllocator {
 public:
  explicit AsidAllocator(enum arm64_asid_width asid_width_override = arm64_asid_width::UNKNOWN);
  ~AsidAllocator();

  zx::result<uint16_t> Alloc();
  zx::result<> Free(uint16_t asid);

  uint16_t max_user_asid() const {
    return (asid_width_ == arm64_asid_width::ASID_8) ? MMU_ARM64_MAX_USER_ASID_8
                                                     : MMU_ARM64_MAX_USER_ASID_16;
  }

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AsidAllocator);

  DECLARE_MUTEX(AsidAllocator) lock_;
  uint16_t last_ TA_GUARDED(lock_) = MMU_ARM64_FIRST_USER_ASID - 1;
  enum arm64_asid_width asid_width_ = arm64_asid_width::UNKNOWN;

  bitmap::RawBitmapGeneric<bitmap::FixedStorage<MMU_ARM64_MAX_USER_ASID_16 + 1>> bitmap_
      TA_GUARDED(lock_);
};

#endif  // ZIRCON_KERNEL_ARCH_ARM64_ASID_ALLOCATOR_H_
