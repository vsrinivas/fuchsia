// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_ARCH_ARM64_ASID_ALLOCATOR_H_
#define ZIRCON_KERNEL_ARCH_ARM64_ASID_ALLOCATOR_H_

#include <zircon/types.h>

#include <arch/arm64/mmu.h>
#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/macros.h>
#include <kernel/mutex.h>

class AsidAllocator {
 public:
  AsidAllocator() { bitmap_.Reset(MMU_ARM64_MAX_USER_ASID + 1); }
  ~AsidAllocator() = default;

  zx_status_t Alloc(uint16_t* asid);
  zx_status_t Free(uint16_t asid);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AsidAllocator);

  DECLARE_MUTEX(AsidAllocator) lock_;
  uint16_t last_ TA_GUARDED(lock_) = MMU_ARM64_FIRST_USER_ASID - 1;

  bitmap::RawBitmapGeneric<bitmap::FixedStorage<MMU_ARM64_MAX_USER_ASID + 1>> bitmap_
      TA_GUARDED(lock_);

  static_assert(MMU_ARM64_ASID_BITS <= 16, "");
};

#endif  // ZIRCON_KERNEL_ARCH_ARM64_ASID_ALLOCATOR_H_
