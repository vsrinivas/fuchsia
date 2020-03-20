// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DISK_INSPECTOR_VMO_BUFFER_FACTORY_H_
#define DISK_INSPECTOR_VMO_BUFFER_FACTORY_H_

#include <lib/fit/result.h>
#include <zircon/types.h>

#include <memory>

#include <storage/buffer/block_buffer.h>
#include <storage/buffer/vmoid_registry.h>

#include "buffer_factory.h"

namespace disk_inspector {

// BufferFactory that is backed by VmoBuffers. This object's lifetime should not exceed the lifetime
// of its associated registry.
class VmoBufferFactory : public BufferFactory {
 public:
  explicit VmoBufferFactory(storage::VmoidRegistry* registry, uint32_t block_size)
      : registry_(registry), block_size_(block_size) {}
  VmoBufferFactory(const VmoBufferFactory&) = delete;
  VmoBufferFactory(VmoBufferFactory&&) = delete;
  VmoBufferFactory& operator=(const VmoBufferFactory&) = delete;
  VmoBufferFactory& operator=(VmoBufferFactory&&) = delete;
  ~VmoBufferFactory() override = default;

  // BufferFactory interface:
  fit::result<std::unique_ptr<storage::BlockBuffer>, zx_status_t> CreateBuffer(
      size_t capacity) const final;

 private:
  // Registry used to register created VmoBuffers with an underlying block device.
  storage::VmoidRegistry* registry_;
  // Block size used for created VmoBuffers.
  uint32_t block_size_;
};

}  // namespace disk_inspector

#endif  // DISK_INSPECTOR_VMO_BUFFER_FACTORY_H_
