// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fifo.h"

#include <fidl/fuchsia.device/cpp/wire.h>

zx_status_t Fifo::Read(uint8_t* buffer, size_t length, size_t* actual) {
  std::lock_guard guard(lock_);

  size_t count;
  for (count = 0; count < length; ++count) {
    if (ReadByteLocked(&buffer[count]) != ZX_OK) {
      break;
    }
  }
  *actual = count;

  if (IsEmptyLocked()) {
    event_.signal_peer(static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable), 0);
  }

  if (count == 0) {
    return ZX_ERR_SHOULD_WAIT;
  }
  return ZX_OK;
}

zx_status_t Fifo::Write(const uint8_t* buffer, size_t length, size_t* actual) {
  std::lock_guard guard(lock_);

  size_t count;
  for (count = 0; count < length; ++count) {
    if (WriteByteLocked(buffer[count]) != ZX_OK) {
      break;
    }
  }
  *actual = count;

  if (!IsEmptyLocked()) {
    event_.signal_peer(0, static_cast<zx_signals_t>(fuchsia_device::wire::DeviceSignal::kReadable));
  }

  if (count == 0) {
    return ZX_ERR_SHOULD_WAIT;
  }
  return ZX_OK;
}

zx_status_t Fifo::ReadByteLocked(uint8_t* out) {
  if (IsEmptyLocked()) {
    return ZX_ERR_SHOULD_WAIT;
  }
  *out = data_[tail_];
  tail_ = (tail_ + 1) & kFifoMask;
  return ZX_OK;
}

zx_status_t Fifo::WriteByteLocked(uint8_t x) {
  if (IsFullLocked()) {
    return ZX_ERR_SHOULD_WAIT;
  }
  data_[head_] = x;
  head_ = (head_ + 1) & kFifoMask;
  return ZX_OK;
}
