// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ringbuffer.h"

bool Ringbuffer::Overwrite32(uint32_t dwords_before_tail, uint32_t value) {
  // The tail points past the last element in the ringbuffer, so 0 is an invalid offset.
  if (dwords_before_tail == 0) {
    return DRETF(false, "Cannot overwrite at zero offset from tail");
  }
  uint32_t offset_bytes = dwords_before_tail * sizeof(uint32_t);
  uint32_t rb_bytes_stored = (tail() >= head())
      ? tail() - head()
      : size() - head() + tail();

  if (rb_bytes_stored < offset_bytes) {
    return DRETF(false, "Invalid offset from tail 0x%x bytes, cur ringbuffer size 0x%x",
                 offset_bytes, rb_bytes_stored);
  }
  uint32_t write_offset = SubtractOffset(offset_bytes);
  DASSERT(write_offset < size());

  vaddr()[write_offset >> 2] = value;
  return true;
}

uint32_t Ringbuffer::SubtractOffset(uint32_t offset_bytes) {
  return (tail() >= offset_bytes)
    ? tail() - offset_bytes
    : size() - offset_bytes + tail();
}
