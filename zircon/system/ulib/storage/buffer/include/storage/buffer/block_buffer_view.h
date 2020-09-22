// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_BUFFER_BLOCK_BUFFER_VIEW_H_
#define STORAGE_BUFFER_BLOCK_BUFFER_VIEW_H_

#include <zircon/assert.h>

#include <storage/buffer/block_buffer.h>

namespace storage {

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
    ZX_ASSERT(length <= buffer->capacity());
  }

  // Creates a new block buffer view within the current view.
  // |relative_start| is relative to |this.start()|.
  // |relative_start + new_length| must be less than or equal to |this.length()|.
  // Otherwise an assertion is fired.
  //
  // Does not modify the original view.
  BlockBufferView CreateSubView(size_t relative_start, size_t new_length) const {
    ZX_ASSERT(relative_start + new_length <= length());
    return BlockBufferView(buffer_, start() + relative_start, new_length);
  }

  // Returns the start of the view, in blocks.
  size_t start() const { return start_; }
  // Returns the length of the view, in blocks.
  size_t length() const { return length_; }
  vmoid_t vmoid() const { return buffer_ ? buffer_->vmoid() : BLOCK_VMOID_INVALID; }

  // Returns one block of data starting at block |index| within this view.
  void* Data(size_t index) {
    return const_cast<void*>(const_cast<const BlockBufferView*>(this)->Data(index));
  }

  uint32_t BlockSize() const { return buffer_ ? buffer_->BlockSize() : 0; }

  const void* Data(size_t index) const {
    ZX_ASSERT_MSG(index < length_, "Accessing data outside the length of the view");
    return buffer_->Data((start_ + index) % buffer_->capacity());
  }

 private:
  BlockBuffer* buffer_ = nullptr;
  size_t start_ = 0;
  size_t length_ = 0;
};

}  // namespace storage

#endif  // STORAGE_BUFFER_BLOCK_BUFFER_VIEW_H_
