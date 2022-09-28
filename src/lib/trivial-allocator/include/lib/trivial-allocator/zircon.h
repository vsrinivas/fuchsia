// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_ZIRCON_H_
#define SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_ZIRCON_H_

#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>

#include <cassert>
#include <utility>

namespace trivial_allocator {

// trivial_allocator::ZirconVmar holds a zx::unowned_vmar and uses it to meet
// the Memory API for trivial_allocator::PageAllocator.
class ZirconVmar {
 public:
  // We use a sub-VMAR as a capability for each allocation so that once it's
  // been sealed, its protections cannot be changed again.  (It can still be
  // unmapped and something else mapped in the same location.)
  using Capability = zx::vmar;

  ZirconVmar() = default;
  ZirconVmar(const ZirconVmar&) = default;

  explicit ZirconVmar(const zx::vmar& vmar) : vmar_(vmar) { assert(vmar_->is_valid()); }

  ZirconVmar& operator=(const ZirconVmar&) = default;

  const zx::vmar& vmar() const { return *vmar_; }

  [[gnu::const]] size_t page_size() const { return zx_system_get_page_size(); }

  [[nodiscard]] std::pair<void*, zx::vmar> Allocate(size_t size) {
    assert(vmar_->is_valid());
    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(size, 0, &vmo);
    if (status == ZX_OK) {
      zx::vmar sub_vmar;
      uintptr_t vmar_address;
      status = vmar_->allocate(ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0, size, &sub_vmar,
                               &vmar_address);
      if (status == ZX_OK) {
        uintptr_t address;
        status = sub_vmar.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo, 0, size, &address);
        if (status == ZX_OK) {
          assert(address >= vmar_address);
          return {reinterpret_cast<void*>(address), std::move(sub_vmar)};
        }
      }
    }
    return {};
  }

  void Deallocate(zx::vmar sub_vmar, void* ptr, size_t size) {
    // Destruction of the VMAR object cleans up the mapping.
    assert(sub_vmar.is_valid());
    [[maybe_unused]] zx_status_t status = sub_vmar.destroy();
    assert(status == ZX_OK);
  }

  // The VMAR handle is consumed here, so there will no longer be any way to
  // "unseal" this allocation (that is, change page protections on the memory).
  void Seal(zx::vmar sub_vmar, void* ptr, size_t size) {
    assert(sub_vmar.is_valid());
    [[maybe_unused]] zx_status_t status =
        sub_vmar.protect(ZX_VM_PERM_READ, reinterpret_cast<uintptr_t>(ptr), size);
    assert(status == ZX_OK);
  }

 private:
  zx::unowned_vmar vmar_;
};

}  // namespace trivial_allocator

#endif  // SRC_LIB_TRIVIAL_ALLOCATOR_INCLUDE_LIB_TRIVIAL_ALLOCATOR_ZIRCON_H_
