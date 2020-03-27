// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_STORAGE_BUFFER_OWNED_VMOID_H_
#define ZIRCON_SYSTEM_ULIB_STORAGE_BUFFER_OWNED_VMOID_H_

#include <storage/buffer/vmoid_registry.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

namespace storage {

// OwnedVmoid manages a VMO attached to the block FIFO, using RAII to disconnect the VMO on
// destruction.
class OwnedVmoid {
 public:
  OwnedVmoid() = default;
  explicit OwnedVmoid(VmoidRegistry* vmo_attacher);
  OwnedVmoid(Vmoid vmoid, VmoidRegistry* registry)
      : vmoid_(std::move(vmoid)), vmoid_registry_(registry) {}
  OwnedVmoid(OwnedVmoid&& other);
  OwnedVmoid& operator=(OwnedVmoid&& other);
  ~OwnedVmoid();

  zx_status_t AttachVmo(const zx::vmo& vmo);
  void Reset();

  bool IsAttached() const { return vmoid_.IsAttached(); }
  vmoid_t get() const { return vmoid_.get(); }

  // Returns a mutable reference to the underlying Vmoid, which allows it to be passed to functions
  // that take a Vmoid* output parameter. Any existing Vmoid will be detached.
  Vmoid& GetReference(VmoidRegistry* registry) {
    Reset();
    vmoid_registry_ = registry;
    return vmoid_;
  }

  vmoid_t TakeId() { return vmoid_.TakeId(); }

 private:
  void MoveFrom(OwnedVmoid&& other);

  Vmoid vmoid_;
  VmoidRegistry* vmoid_registry_ = nullptr;
};

}  // namespace storage

#endif  // ZIRCON_SYSTEM_ULIB_STORAGE_BUFFER_OWNED_VMOID_H_
