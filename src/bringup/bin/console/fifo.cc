// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fifo.h"

#include <fuchsia/io/llcpp/fidl.h>

#include <fbl/auto_lock.h>

zx_status_t Fifo::Read(uint8_t* buffer, size_t length, size_t* actual) {
  fbl::AutoLock guard(&lock_);

  size_t count;
  for (count = 0; count < length; ++count) {
    if (ReadByteLocked(&buffer[count]) != ZX_OK) {
      break;
    }
  }
  *actual = count;

  if (IsEmptyLocked()) {
    event_.signal_peer(::llcpp::fuchsia::io::DEVICE_SIGNAL_READABLE, 0);
  }

  if (count == 0) {
    return ZX_ERR_SHOULD_WAIT;
  }
  return ZX_OK;
}

zx_status_t Fifo::Write(const uint8_t* buffer, size_t length, size_t* actual) {
  fbl::AutoLock guard(&lock_);

  size_t count;
  for (count = 0; count < length; ++count) {
    if (WriteByteLocked(buffer[count]) != ZX_OK) {
      break;
    }
  }
  *actual = count;

  if (!IsEmptyLocked()) {
    event_.signal_peer(0, ::llcpp::fuchsia::io::DEVICE_SIGNAL_READABLE);
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
