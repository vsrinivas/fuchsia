// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/vmo/shared_vmo.h"

#include <lib/zx/vmar.h>

#include "lib/fxl/logging.h"

namespace fsl {

static_assert(sizeof(size_t) == sizeof(uint64_t), "64-bit architecture");

SharedVmo::SharedVmo(zx::vmo vmo, uint32_t map_flags)
    : vmo_(std::move(vmo)), map_flags_(map_flags) {
  FXL_DCHECK(vmo_);

  zx_status_t status = vmo_.get_size(&vmo_size_);
  FXL_CHECK(status == ZX_OK);
}

SharedVmo::~SharedVmo() {
  if (mapping_) {
    zx_status_t status = zx::vmar::root_self()->unmap(mapping_, vmo_size_);
    FXL_CHECK(status == ZX_OK);
  }
}

void* SharedVmo::Map() {
  if (vmo_ && map_flags_) {
    std::call_once(mapping_once_flag_, [this] {
      // If an error occurs, then |mapping_| will remain 0.
      zx_status_t status = zx::vmar::root_self()->map(0, vmo_, 0u, vmo_size_,
                                                      map_flags_, &mapping_);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to map vmo: vmo_size=" << vmo_size_
                       << ", map_flags=" << map_flags_ << ", status=" << status;
      }
    });
  }
  return reinterpret_cast<void*>(mapping_);
}

}  // namespace fsl
