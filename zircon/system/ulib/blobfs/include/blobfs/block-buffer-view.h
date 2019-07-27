// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_BLOCK_BUFFER_VIEW_H_
#define BLOBFS_BLOCK_BUFFER_VIEW_H_

#include <zircon/assert.h>

#include <blobfs/block-buffer.h>

namespace blobfs {

// A wrap-around view into a portion of a BlockBuffer, a block-aligned buffer.
//
// Does not own the BlockBuffer. Caution must be taken when using BlockBufferView to not
// outlive the source BlockBuffer object. This is akin to a "StringView" object for a string.
//
// This class is movable and copyable.
// This class is thread-compatible.
class BlockBufferView {
 public:
  BlockBufferView() = default;
  BlockBufferView(BlockBuffer* buffer, size_t start, size_t length)
      : buffer_(buffer), start_(start % buffer->capacity()), length_(length) {
    ZX_DEBUG_ASSERT(length <= buffer->capacity());
  }
  BlockBufferView(const BlockBufferView&) = default;
  BlockBufferView& operator=(const BlockBufferView&) = default;
  BlockBufferView(BlockBufferView&& other) = default;
  BlockBufferView& operator=(BlockBufferView&& other) = default;
  ~BlockBufferView() = default;

  // Returns the start of the view, in blocks.
  size_t start() const { return start_; }
  // Returns the length of the view, in blocks.
  size_t length() const { return length_; }
  vmoid_t vmoid() const { return buffer_ ? buffer_->vmoid() : VMOID_INVALID; }

  // Returns one block of data starting at block |index| within this view.
  void* Data(size_t index) {
    return const_cast<void*>(const_cast<const BlockBufferView*>(this)->Data(index));
  }

  const void* Data(size_t index) const {
    ZX_DEBUG_ASSERT_MSG(index < length_, "Accessing data outside the length of the view");
    return buffer_->Data((start_ + index) % buffer_->capacity());
  }

 private:
  BlockBuffer* buffer_ = nullptr;
  size_t start_ = 0;
  size_t length_ = 0;
};

}  // namespace blobfs

#endif  // BLOBFS_BLOCK_BUFFER_VIEW_H_
