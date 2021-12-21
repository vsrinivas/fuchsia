// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_MINFS_RESIZEABLE_VMO_BUFFER_H_
#define SRC_STORAGE_MINFS_RESIZEABLE_VMO_BUFFER_H_

#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/zx/status.h>
#include <zircon/compiler.h>

#include <storage/buffer/block_buffer.h>
#include <storage/buffer/vmoid_registry.h>

namespace minfs {

// A resizeable VMO buffer. The buffer isn't usable until Attach is called.
class ResizeableVmoBuffer : public storage::BlockBuffer {
 public:
  using Handle = vmoid_t;

  ResizeableVmoBuffer(uint32_t block_size) : block_size_(block_size) {}

  // BlockBuffer interface:
  size_t capacity() const override { return vmo_.size() / block_size_; }
  uint32_t BlockSize() const override { return block_size_; }
  vmoid_t vmoid() const override { return vmoid_.get(); }
  zx_handle_t Vmo() const override { return vmo_.vmo().get(); }
  void* Data(size_t index) override {
    return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(vmo_.start()) + index * block_size_);
  }
  const void* Data(size_t index) const override {
    return reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(vmo_.start()) +
                                         index * block_size_);
  }

  const zx::vmo& vmo() { return vmo_.vmo(); }
  zx::status<> Grow(size_t block_count) {
    return zx::make_status(vmo_.Grow(block_count * block_size_));
  }
  zx::status<> Shrink(size_t block_count) {
    return zx::make_status(vmo_.Shrink(block_count * block_size_));
  }

  // Avoid using this method unless *absolutely* necessary. Eventually, other interfaces that take
  // different handle types should go away and this should no longer be required.
  Handle GetHandle() { return vmoid(); }

  [[nodiscard]] zx::status<> Attach(const char* name, storage::VmoidRegistry* device);
  zx::status<> Detach(storage::VmoidRegistry* device);

  void Zero(size_t index, size_t count) override;

 private:
  ResizeableVmoBuffer();

  uint32_t block_size_;
  fzl::ResizeableVmoMapper vmo_;
  storage::Vmoid vmoid_;
};

}  // namespace minfs

#endif  // SRC_STORAGE_MINFS_RESIZEABLE_VMO_BUFFER_H_
