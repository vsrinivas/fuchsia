// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iobuffer.h"

IoBuffer::IoBuffer(zx::vmo vmo, vmoid_t id) : io_vmo_(std::move(vmo)), vmoid_(id) {}

IoBuffer::~IoBuffer() = default;

zx_status_t IoBuffer::ValidateVmoHack(uint64_t length, uint64_t vmo_offset) {
  uint64_t vmo_size;
  if (zx_status_t status = io_vmo_.get_size(&vmo_size); status != ZX_OK) {
    return status;
  }
  if ((vmo_offset > vmo_size) || (vmo_size - vmo_offset < length)) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  return ZX_OK;
}
