// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DISK_INSPECTOR_BUFFER_FACTORY_H_
#define DISK_INSPECTOR_BUFFER_FACTORY_H_

#include <lib/fit/result.h>
#include <zircon/types.h>

#include <memory>

#include <storage/buffer/block_buffer.h>

namespace disk_inspector {

// Generic interface to dispense block buffers. Classes or functions that need
// to use block buffers and intend to be operating system agnostic should
// take in a |BufferFactory| to create generic BlockBuffers.
class BufferFactory {
 public:
  virtual ~BufferFactory() = default;

  // Creates a block buffer of size |capacity| to store in |out|.
  virtual fit::result<std::unique_ptr<storage::BlockBuffer>, zx_status_t> CreateBuffer(
      size_t capacity) const = 0;
};

}  // namespace disk_inspector

#endif  // DISK_INSPECTOR_BUFFER_FACTORY_H_
