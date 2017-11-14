// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/fsl/vmo/sized_vmo.h"

#include "lib/fxl/logging.h"

namespace fsl {

SizedVmo::SizedVmo() : vmo_(), size_(0u) {}

SizedVmo::SizedVmo(zx::vmo vmo, uint64_t size)
    : vmo_(std::move(vmo)), size_(size) {
  FXL_DCHECK(!vmo_ || IsSizeValid(vmo_, size_));
}

bool SizedVmo::IsSizeValid(const zx::vmo& vmo, uint64_t size) {
  size_t vmo_size;
  zx_status_t zx_status = vmo.get_size(&vmo_size);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to get VMO size. Error: " << zx_status;
    return false;
  }
  return vmo_size >= size;
}

SizedVmo::SizedVmo(SizedVmo&& other) = default;

SizedVmo::~SizedVmo() = default;

SizedVmo& SizedVmo::operator=(SizedVmo&& other) = default;

zx_status_t SizedVmo::Duplicate(zx_rights_t rights, SizedVmo* output) const {
  zx_status_t status =
      vmo_.duplicate(rights, &output->vmo_);
  if (status == ZX_OK) {
    output->size_ = size_;
  }
  return status;
}

}  // namespace fsl
