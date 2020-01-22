// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ringbuffer.h"

bool Ringbuffer::IsOffsetPopulated(uint32_t offset) {
  if (offset >= size()) {
    return false;
  }
  return (head() <= tail()) ?
    ((offset >= head()) && (offset < tail())) :
    ((offset >= head()) || (offset < tail()));
}

bool Ringbuffer::Overwrite32(uint32_t offset, uint32_t value) {
  if (!IsOffsetPopulated(offset)) {
    return DRETF(false, "Invalid rb offset %u, head %u tail %u", offset, head(), tail());
  }
  vaddr()[offset >> 2] = value;
  return true;
}

uint32_t Ringbuffer::SubtractOffset(uint32_t offset_bytes) {
  return (tail() >= offset_bytes)
    ? tail() - offset_bytes
    : size() - offset_bytes + tail();
}

bool Ringbuffer::ReserveContiguous(uint32_t reserve_bytes) {
  if (!HasSpace(reserve_bytes)) {
    return DRETF(false, "Ringbuffer does not have space for %u bytes", reserve_bytes);
  }
  // If there are not at least |reserve_bytes| number of contiguous bytes,
  // we will need to advance the tail to the start of the ringbuffer.
  uint32_t bytes_until_end = size() - tail();
  if (bytes_until_end < reserve_bytes) {
    if (!HasSpace(reserve_bytes + bytes_until_end)) {
      return DRETF(false, "Ringbuffer does not have contiguous space for %u bytes",
                   reserve_bytes);
    }
    update_tail(0);
    DASSERT(tail() != head());
  }
  return true;
}
