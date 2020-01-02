// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFER_TEST_SUPPORT_ARRAY_BUFFER_H_
#define BUFFER_TEST_SUPPORT_ARRAY_BUFFER_H_

#include <array>

#include <storage/buffer/block-buffer.h>

namespace storage {

// Block buffer backed by a heap array.
class ArrayBuffer final : public BlockBuffer {
 public:
  explicit ArrayBuffer(size_t capacity, uint32_t block_size);
  ArrayBuffer(const ArrayBuffer&) = delete;
  ArrayBuffer(ArrayBuffer&&) = default;
  ArrayBuffer& operator=(const ArrayBuffer&) = delete;
  ArrayBuffer& operator=(ArrayBuffer&&) = default;
  ~ArrayBuffer() = default;

  // BlockBuffer interface:
  size_t capacity() const final { return capacity_; }

  uint32_t BlockSize() const final { return block_size_; }

  vmoid_t vmoid() const final { return BLOCK_VMOID_INVALID; }

  void* Data(size_t index) final;

  const void* Data(size_t index) const final;

 private:
  std::unique_ptr<char[]> buffer_;
  uint32_t block_size_ = 0;
  size_t capacity_ = 0;
};

}  // namespace storage

#endif  // BUFFER_TEST_SUPPORT_ARRAY_BUFFER_H_
