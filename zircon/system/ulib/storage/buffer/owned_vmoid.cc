// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/owned_vmoid.h"

#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/status.h>

#include "storage/buffer/vmoid_registry.h"

namespace storage {

OwnedVmoid::OwnedVmoid(storage::VmoidRegistry* vmoid_registry)
    : vmoid_registry_(vmoid_registry) {
  ZX_ASSERT(vmoid_registry_ != nullptr);
}

OwnedVmoid::OwnedVmoid(OwnedVmoid&& other) { MoveFrom(std::move(other)); }

OwnedVmoid& OwnedVmoid::operator=(OwnedVmoid&& other) {
  MoveFrom(std::move(other));
  return *this;
}

OwnedVmoid::~OwnedVmoid() {
  Reset();
}

zx_status_t OwnedVmoid::AttachVmo(const zx::vmo& vmo) {
  zx_status_t status;
  if ((status = vmoid_registry_->BlockAttachVmo(vmo, &vmoid_)) != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

void OwnedVmoid::Reset() {
  if (vmoid_.IsAttached()) {
    vmoid_registry_->BlockDetachVmo(std::move(vmoid_));
  }
}

void OwnedVmoid::MoveFrom(OwnedVmoid&& other) {
  Reset();
  vmoid_registry_ = other.vmoid_registry_;
  vmoid_ = std::move(other.vmoid_);
}

}  // namespace storage
