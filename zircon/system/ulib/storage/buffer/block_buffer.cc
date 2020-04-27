// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <storage/buffer/block_buffer.h>

#include <cstring>
#include <zircon/assert.h>

namespace storage {

void BlockBuffer::Zero(size_t index, size_t count) {
  ZX_ASSERT(index + count <= capacity());
  std::memset(Data(index), 0, count * BlockSize());
}

}  // namespace storage
