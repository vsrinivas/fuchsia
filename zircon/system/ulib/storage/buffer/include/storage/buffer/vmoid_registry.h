// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BUFFER_VMOID_REGISTRY_H_
#define STORAGE_BUFFER_VMOID_REGISTRY_H_

#include <lib/zx/vmo.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

namespace storage {

// A thin wrapper around a vmoid_t that will assert if you forget to detach it.
class Vmoid {
 public:
  Vmoid() = default;
  explicit Vmoid(vmoid_t vmoid) : vmoid_(vmoid) {}

  // Movable but not copyable.
  Vmoid(Vmoid& other) = delete;
  Vmoid& operator =(Vmoid& other) = delete;

  Vmoid(Vmoid&& other) {
    vmoid_ = other.vmoid_;
    other.vmoid_ = BLOCK_VMOID_INVALID;
  }
  Vmoid& operator =(Vmoid&& other) {
    ZX_DEBUG_ASSERT(vmoid_ == BLOCK_VMOID_INVALID);
    vmoid_ = other.vmoid_;
    other.vmoid_ = 0;
    return *this;
  }

  ~Vmoid() {
    ZX_DEBUG_ASSERT_MSG(vmoid_ == BLOCK_VMOID_INVALID, "%u", vmoid_);
  }

  vmoid_t get() const { return vmoid_; }
  bool IsAttached() const { return vmoid_ != BLOCK_VMOID_INVALID; }
  [[nodiscard]] vmoid_t TakeId() {
    vmoid_t id = vmoid_;
    vmoid_ = BLOCK_VMOID_INVALID;
    return id;
  }

 private:
  vmoid_t vmoid_ = BLOCK_VMOID_INVALID;
};

// An interface which controls attaching and detaching VMOs with the underlying device.
class VmoidRegistry {
 public:
  virtual ~VmoidRegistry() = default;

  // Allocates a vmoid registering a VMO with the underlying block device.
  virtual zx_status_t BlockAttachVmo(const zx::vmo& vmo, Vmoid* out) = 0;

  // Releases an allocated vmoid.
  virtual zx_status_t BlockDetachVmo(Vmoid vmoid) = 0;
};

}  // namespace storage

#endif  // STORAGE_BUFFER_VMOID_REGISTRY_H_
