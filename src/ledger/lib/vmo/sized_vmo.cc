// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/vmo/sized_vmo.h"

#include <zircon/status.h>

#include "src/lib/fxl/logging.h"

namespace ledger {

SizedVmo::SizedVmo(std::nullptr_t) : vmo_(), size_(0u) {}

SizedVmo::SizedVmo(zx::vmo vmo, uint64_t size) : vmo_(std::move(vmo)), size_(size) {
  FXL_DCHECK(vmo_ && IsSizeValid(vmo_, size_));
}

bool SizedVmo::FromTransport(fuchsia::mem::Buffer transport, SizedVmo* out) {
  FXL_DCHECK(transport.vmo);

  if (!IsSizeValid(transport.vmo, transport.size)) {
    return false;
  }

  *out = SizedVmo(std::move(transport.vmo), transport.size);
  return true;
}

bool SizedVmo::IsSizeValid(const zx::vmo& vmo, uint64_t size) {
  size_t vmo_size;
  zx_status_t zx_status = vmo.get_size(&vmo_size);
  if (zx_status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to get VMO size: " << zx_status_get_string(zx_status);
    return false;
  }
  return vmo_size >= size;
}

SizedVmo::SizedVmo(SizedVmo&& other) : vmo_(std::move(other.vmo_)), size_(other.size_) {
  other.size_ = 0;
}

SizedVmo::~SizedVmo() = default;

SizedVmo& SizedVmo::operator=(SizedVmo&& other) {
  vmo_ = std::move(other.vmo_);
  size_ = other.size_;
  other.size_ = 0;
  return *this;
}

fuchsia::mem::Buffer SizedVmo::ToTransport() && {
  fuchsia::mem::Buffer result;
  if (!vmo_) {
    return result;
  }
  result.vmo = std::move(vmo_);
  result.size = size_;
  size_ = 0;
  return result;
}

zx_status_t SizedVmo::Duplicate(zx_rights_t rights, SizedVmo* output) const {
  zx_status_t status = vmo_.duplicate(rights, &output->vmo_);
  if (status == ZX_OK) {
    output->size_ = size_;
  }
  return status;
}

zx_status_t SizedVmo::ReplaceAsExecutable(const zx::handle& vmex) {
  return vmo_.replace_as_executable(vmex, &vmo_);
}

}  // namespace ledger
