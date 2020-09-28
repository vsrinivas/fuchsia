// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_UNOWNED_VMO_BUFFER_H_
#define SRC_STORAGE_MINFS_UNOWNED_VMO_BUFFER_H_

#ifdef __Fuchsia__

#include <lib/zx/vmo.h>

#include <storage/buffer/block_buffer.h>

namespace minfs {

// Trivial BlockBuffer that doesn't own the underlying buffer.
// TODO(fxbug.dev/47947): Remove this.
class UnownedVmoBuffer : public storage::BlockBuffer {
 public:
  UnownedVmoBuffer(const zx::unowned_vmo& vmo) : vmo_(vmo) {}
  ~UnownedVmoBuffer() {}

  // BlockBuffer interface:
  size_t capacity() const final { return 0; }
  uint32_t BlockSize() const final { return 0; }
  vmoid_t vmoid() const final { return 0; }
  zx_handle_t Vmo() const final { return vmo_->get(); }
  void* Data(size_t index) final { return nullptr; }
  const void* Data(size_t index) const final { return nullptr; }

 private:
  zx::unowned_vmo vmo_;
};

}  // namespace minfs

#endif  // __Fuchsia__

#endif  // SRC_STORAGE_MINFS_UNOWNED_VMO_BUFFER_H_
