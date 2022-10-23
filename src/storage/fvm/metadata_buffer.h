// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_METADATA_BUFFER_H_
#define SRC_STORAGE_FVM_METADATA_BUFFER_H_

#include <lib/zx/result.h>
#include <zircon/types.h>

#include <limits>

#include "src/storage/fvm/format.h"

namespace fvm {

// MetadataBuffer is an interface for a buffer that contains FVM metadata.
class MetadataBuffer {
 public:
  virtual ~MetadataBuffer() = default;

  // Creates an uninitialized |MetadataBuffer| which has capacity for at least |size| bytes.
  // This is intentionally non-static so inheriting classes can override it to return the
  // appropriate type. In general the instance's fields/methods will not be accessed.
  virtual std::unique_ptr<MetadataBuffer> Create(size_t size) const = 0;

  virtual void* data() const = 0;
  virtual size_t size() const = 0;
};

// HeapMetadataBuffer is an instance of |MetadataBuffer| backed by a heap-allocated buffer.
class HeapMetadataBuffer : public MetadataBuffer {
 public:
  HeapMetadataBuffer(std::unique_ptr<uint8_t[]> buffer, size_t size);
  ~HeapMetadataBuffer() override;

  std::unique_ptr<MetadataBuffer> Create(size_t size) const override;

  void* data() const override { return buffer_.get(); }
  size_t size() const override { return size_; }

 private:
  std::unique_ptr<uint8_t[]> buffer_;
  size_t size_;
};

}  // namespace fvm

#endif  // SRC_STORAGE_FVM_METADATA_BUFFER_H_
