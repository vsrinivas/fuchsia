// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BUFFER_VMOID_REGISTRY_H_
#define STORAGE_BUFFER_VMOID_REGISTRY_H_

#include <lib/zx/vmo.h>
#include <zircon/device/block.h>
#include <zircon/types.h>

namespace storage {

// An interface which controls attaching and detaching VMOs with the underlying device.
class VmoidRegistry {
 public:
  virtual ~VmoidRegistry() = default;

  // Allocates a vmoid registering a VMO with the underlying block device.
  virtual zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) = 0;

  // Releases an allocated vmoid.
  virtual zx_status_t DetachVmo(vmoid_t vmoid) = 0;
};

}  // namespace storage

#endif  // STORAGE_BUFFER_VMOID_REGISTRY_H_
