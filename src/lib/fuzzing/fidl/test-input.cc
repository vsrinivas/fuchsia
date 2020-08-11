// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-input.h"

#include <lib/zx/vmar.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/status.h>

#include <algorithm>

namespace fuzzing {

const size_t TestInput::kVmoSize;
const size_t TestInput::kMaxInputSize = kVmoSize - sizeof(uint64_t);

TestInput::TestInput() {}

TestInput::~TestInput() {}

zx_status_t TestInput::Create(size_t len) {
  if (len != kVmoSize) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  return SharedMemory::Create(kVmoSize);
}

zx_status_t TestInput::Link(const zx::vmo &vmo, size_t len) {
  if (len != kVmoSize) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t status = SharedMemory::Link(vmo, kVmoSize);
  if (status != ZX_OK) {
    return status;
  }
  data_ = reinterpret_cast<uint8_t *>(addr() + sizeof(uint64_t));
  size_ = reinterpret_cast<uint64_t *>(addr());
  return ZX_OK;
}

zx_status_t TestInput::Write(const void *data, size_t size) const {
  if (!size_) {
    return ZX_ERR_BAD_STATE;
  }
  uint64_t offset = *size_;
  size_t len = std::min(size, kMaxInputSize - offset);
  memcpy(&data_[offset], data, len);
  *size_ = offset + len;
  return len == size ? ZX_OK : ZX_ERR_BUFFER_TOO_SMALL;
}

zx_status_t TestInput::Clear() const {
  if (!size_) {
    return ZX_ERR_BAD_STATE;
  }
  *size_ = 0;
  return ZX_OK;
}

}  // namespace fuzzing
