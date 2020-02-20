// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_STORAGE_BUFFER_OWNED_VMOID_H_
#define ZIRCON_SYSTEM_ULIB_STORAGE_BUFFER_OWNED_VMOID_H_

#include <zircon/device/block.h>
#include <zircon/types.h>

#include <optional>

#include <fbl/macros.h>
#include <storage/buffer/vmoid_registry.h>

namespace storage {

// OwnedVmoid manages a VMO attached to the block FIFO, using RAII to disconnect the VMO on
// destruction.
class OwnedVmoid {
 public:
  OwnedVmoid() = delete;
  explicit OwnedVmoid(storage::VmoidRegistry* vmo_attacher);
  OwnedVmoid(OwnedVmoid&& other);
  OwnedVmoid& operator=(OwnedVmoid&& other);
  ~OwnedVmoid();

  zx_status_t AttachVmo(const zx::vmo& vmo);
  void Reset();

  bool attached() const { return vmoid_.has_value(); }
  vmoid_t vmoid() const { return *vmoid_; }

 private:
  storage::VmoidRegistry* vmoid_registry_;
  std::optional<vmoid_t> vmoid_;

  void MoveFrom(OwnedVmoid&& other);

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(OwnedVmoid);
};

}  // namespace storage

#endif  // ZIRCON_SYSTEM_ULIB_STORAGE_BUFFER_OWNED_VMOID_H_
