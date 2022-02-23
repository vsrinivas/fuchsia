// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <cstring>

#include <safemath/checked_math.h>
#include <storage/buffer/block_buffer.h>

namespace storage {

zx_status_t BlockBuffer::Zero(size_t index, size_t count) {
  auto end_index_or = safemath::CheckAdd(index, count);
  auto length_or = safemath::CheckMul(count, safemath::checked_cast<size_t>(BlockSize()));
  if (!length_or.IsValid() || !end_index_or.IsValid() || end_index_or.ValueOrDie() > capacity()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  std::memset(Data(index), 0, length_or.ValueOrDie());
  return ZX_OK;
}

}  // namespace storage
