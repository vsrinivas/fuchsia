// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/phys_mem.h"

#include <lib/zx/vmar.h>
#include <zircon/process.h>

static constexpr uint32_t kMapFlags =
    ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;

namespace machina {

zx_status_t PhysMem::Init(size_t size) {
  zx_status_t status = zx::vmo::create(size, ZX_VMO_NON_RESIZABLE, &vmo_);
  if (status != ZX_OK) {
    return status;
  }

  status = zx::vmar::root_self()->map(0, vmo_, 0, size, kMapFlags, &addr_);
  if (status != ZX_OK) {
    vmo_.reset();
    return status;
  }

  vmo_size_ = size;
  return ZX_OK;
}

PhysMem::~PhysMem() {
  if (addr_ != 0) {
    zx::vmar::root_self()->unmap(addr_, vmo_size_);
  }
}

}  // namespace machina
