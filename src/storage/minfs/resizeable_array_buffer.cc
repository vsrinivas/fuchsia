// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/resizeable_array_buffer.h"

namespace minfs {

zx_status_t ResizeableArrayBuffer::Shrink(size_t block_count) {
  ZX_ASSERT(block_count > 0 && block_count <= capacity());
  buffer().resize(block_count * BlockSize());
  return ZX_OK;
}

zx_status_t ResizeableArrayBuffer::Grow(size_t block_count) {
  ZX_ASSERT(block_count >= capacity());
  buffer().resize(block_count * BlockSize());
  return ZX_OK;
}

}  // namespace minfs
