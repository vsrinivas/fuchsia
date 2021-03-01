// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_RESIZEABLE_ARRAY_BUFFER_H_
#define SRC_STORAGE_MINFS_RESIZEABLE_ARRAY_BUFFER_H_

#include <zircon/compiler.h>

#include <storage/buffer/array_buffer.h>

#include "src/lib/storage/vfs/cpp/transaction/transaction_handler.h"

namespace minfs {

class Bcache;

class ResizeableArrayBuffer : public storage::ArrayBuffer {
 public:
  using Handle = void*;

  explicit ResizeableArrayBuffer(uint32_t block_size) : ArrayBuffer(1, block_size) {}
  explicit ResizeableArrayBuffer(size_t capacity, uint32_t block_size)
      : ArrayBuffer(capacity, block_size) {}

  // Avoid using this method unless *absolutely* necessary. Eventually, other interfaces that take
  // different handle types should go away and this should no longer be required.
  Handle GetHandle() { return Data(0); }

  [[nodiscard]] zx_status_t Attach(const char* name, fs::TransactionHandler* device) {
    return ZX_OK;
  }
  zx_status_t Detach(fs::TransactionHandler* device) { return ZX_OK; }

  [[nodiscard]] zx_status_t Shrink(size_t block_count);
  [[nodiscard]] zx_status_t Grow(size_t block_count);
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_RESIZEABLE_ARRAY_BUFFER_H_
