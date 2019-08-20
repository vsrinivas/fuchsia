// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_BUFFER_BLOCK_BUFFER_H_
#define FS_BUFFER_BLOCK_BUFFER_H_

#include <zircon/device/block.h>

#include <cstdint>

namespace fs {

// Interface for a block-aligned buffer.
//
// This class should be thread-compatible.
class BlockBuffer {
 public:
  virtual ~BlockBuffer() = default;

  // Returns the total amount of blocks which the buffer handles.
  virtual size_t capacity() const = 0;

  // Returns the size of each data block handled by this buffer.
  virtual uint32_t BlockSize() const = 0;

  // Returns the vmoid of the underlying BlockBuffer, if one exists.
  virtual vmoid_t vmoid() const = 0;

  // Returns data starting at block |index| in the buffer.
  virtual void* Data(size_t index) = 0;

  // Returns data starting at block |index| in the buffer.
  virtual const void* Data(size_t index) const = 0;
};

}  // namespace fs

#endif  // FS_BUFFER_BLOCK_BUFFER_H_
