// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/cpp/vmo/limits.h>
#include <lib/inspect/cpp/vmo/scanner.h>

namespace inspect {
namespace internal {

zx_status_t ScanBlocks(const uint8_t* buffer, size_t size,
                       fit::function<bool(BlockIndex, const Block*)> callback) {
  size_t offset = 0;
  while (offset < size) {
    auto* block = reinterpret_cast<const Block*>(buffer + offset);
    if (size - offset < sizeof(Block)) {
      // Block header does not fit in remaining space.
      return ZX_ERR_OUT_OF_RANGE;
    }
    BlockOrder order = GetOrder(block);
    if (order > kMaxOrderShift) {
      return ZX_ERR_OUT_OF_RANGE;
    }
    if (size - offset < OrderToSize(order)) {
      // Block header specifies an order that is too large to fit
      // in the remainder of the buffer.
      return ZX_ERR_OUT_OF_RANGE;
    }

    if (!callback(IndexForOffset(offset), block)) {
      return ZX_OK;
    }
    offset += OrderToSize(order);
  }

  return ZX_OK;
}

}  // namespace internal
}  // namespace inspect
