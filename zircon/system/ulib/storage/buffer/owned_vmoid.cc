// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/owned_vmoid.h"

#include <zircon/assert.h>
#include <zircon/device/block.h>
#include <zircon/status.h>

#include <optional>

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
  vmoid_t vmoid;
  zx_status_t status;
  if ((status = vmoid_registry_->AttachVmo(vmo, &vmoid)) != ZX_OK) {
    return status;
  }
  vmoid_ = vmoid;
  return ZX_OK;
}

void OwnedVmoid::Reset() {
  if (vmoid_) {
    vmoid_registry_->DetachVmo(*vmoid_);
    vmoid_ = std::nullopt;
  }
}

void OwnedVmoid::MoveFrom(OwnedVmoid&& other) {
  vmoid_registry_ = other.vmoid_registry_;
  vmoid_ = other.vmoid_;
  other.vmoid_ = std::nullopt;
}

}  // namespace storage
