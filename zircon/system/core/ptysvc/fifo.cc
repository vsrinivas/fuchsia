// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fifo.h"

#include <string.h>

static_assert((Fifo::kSize & (Fifo::kSize - 1)) == 0, "fifo size not power of two");

namespace {

constexpr uint32_t kFifoMask = Fifo::kSize - 1;

}  // namespace

size_t Fifo::Write(const void* buf, size_t len, bool atomic) {
  size_t avail = kSize - (head_ - tail_);
  if (avail < len) {
    if (atomic) {
      return 0;
    }
    len = avail;
  }

  size_t offset = head_ & kFifoMask;

  avail = kSize - offset;
  if (len <= avail) {
    memcpy(data_.data() + offset, buf, len);
  } else {
    memcpy(data_.data() + offset, buf, avail);
    memcpy(data_.data(), reinterpret_cast<const uint8_t*>(buf) + avail, len - avail);
  }

  head_ += static_cast<uint32_t>(len);
  return len;
}

size_t Fifo::Read(void* buf, size_t len) {
  size_t avail = head_ - tail_;
  if (avail < len) {
    len = avail;
  }

  size_t offset = tail_ & kFifoMask;

  avail = kSize - offset;
  if (len <= avail) {
    memcpy(buf, data_.data() + offset, len);
  } else {
    memcpy(buf, data_.data() + offset, avail);
    memcpy(reinterpret_cast<uint8_t*>(buf) + avail, data_.data(), len - avail);
  }

  tail_ += static_cast<uint32_t>(len);
  return len;
}
