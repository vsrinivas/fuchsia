// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLOBFS_BLOCK_BUFFER_H_
#define BLOBFS_BLOCK_BUFFER_H_

#include <zircon/device/block.h>

namespace blobfs {

// Interface for a block-aligned buffer.
//
// This class should be thread-compatible.
class BlockBuffer {
 public:
  virtual ~BlockBuffer() = default;

  // Returns the total amount of pending blocks which may be buffered.
  virtual size_t capacity() const = 0;

  // Returns the vmoid of the underlying BlockBuffer, if one exists.
  virtual vmoid_t vmoid() const = 0;

  // Returns data starting at block |index| in the buffer.
  virtual void* Data(size_t index) = 0;

  // Returns data starting at block |index| in the buffer.
  virtual const void* Data(size_t index) const = 0;
};

}  // namespace blobfs

#endif  // BLOBFS_BLOCK_BUFFER_H_
