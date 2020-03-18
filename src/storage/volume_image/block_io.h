// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_BLOCK_IO_H_
#define SRC_STORAGE_VOLUME_IMAGE_BLOCK_IO_H_

#include <string>

#include <fbl/span.h>

namespace storage::volume_image {

// Provides a block based IO interface, in order to facilitate reading from storage.
//
// Some devices might not implement posix block IO operations, so this layers must provide
// an emulation for such behavior.
class BlockReader {
 public:
  virtual ~BlockReader() = default;

  // Block size used for this IO layer.
  virtual uint64_t block_size() const = 0;

  // Returns empty string when contents of 'block device' at [|offset|, |offset| + |buffer.size() /
  // block_size()|) block range are read into |buffer|.
  //
  // On error the returned result to contains a string describing the error.
  //
  // Precondition:
  //   * |buffer.size()| % block_size == 0.
  virtual std::string Read(uint64_t offset, fbl::Span<uint8_t> buffer) = 0;
};

// Provides a block based IO interface, in order to facilitate the writing to storage.
//
// Some devices might not implement posix block IO operations, so this layers must provide
// an emulation for such behavior.
class BlockWriter {
 public:
  virtual ~BlockWriter() = default;

  // Block size used for this IO layer.
  virtual uint64_t block_size() const = 0;

  // Returns empty string when contents of 'block device' at [|offset|, |offset| + |buffer.size() /
  // block_size()|) block range are updated to |buffer|.
  //
  // On error the returned result to contains a string describing the error.
  //
  // Precondition:
  //   * |buffer.size()| % block_size == 0.
  virtual std::string Write(uint64_t offset, fbl::Span<const uint8_t> buffer) = 0;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_BLOCK_IO_H_
