// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/vk/buffer_factory.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"

namespace sketchy_service {

enum class BufferType {
  kVertex = 1,
  kIndex = 2,
};

// Buffer encapsulates an Escher buffer and a Scenic buffer, which share the
// same memory.  The Escher buffer is exported as a VMO, which is used to
// create the Scenic buffer.
class Buffer {
 public:
  static std::unique_ptr<Buffer> New(scenic_lib::Session* session,
                                     escher::BufferFactory* factory,
                                     BufferType type,
                                     vk::DeviceSize capacity);

  Buffer(scenic_lib::Session* session,
         escher::BufferFactory* factory,
         vk::DeviceSize capacity,
         vk::BufferUsageFlags flags);

  // Record the merge buffer command. The command would grow the buffer if
  // necessary.
  void Merge(escher::impl::CommandBuffer* command,
             escher::BufferFactory* factory,
             escher::BufferPtr new_escher_buffer);

  // Preserve a chunk of |size| for use. If the required capacity exceeds the
  // current capacity, the buffer will grow dynamically. The possible copy
  // command will be recorded to |command|. Return the preserved buffer.
  escher::BufferPtr PreserveBuffer(escher::impl::CommandBuffer* command,
                                   escher::BufferFactory* factory,
                                   vk::DeviceSize size);

  const escher::BufferPtr& escher_buffer() const { return escher_buffer_; }
  const scenic_lib::Buffer& scenic_buffer() const {
    return *scenic_buffer_.get();
  }
  vk::DeviceSize capacity() const { return escher_buffer_->size(); }
  vk::DeviceSize size() const { return size_; }

 private:
  // Preserve a chunk of |size| for use. If the required capacity exceeds the
  // current capacity, a larger buffer will be allocated, and the original data
  // will be copied to the new buffer. The copy command will be recorded to
  // |command|.
  void PreserveSize(escher::impl::CommandBuffer* command,
                    escher::BufferFactory* factory,
                    vk::DeviceSize size);

  scenic_lib::Session* const session_;
  escher::BufferPtr escher_buffer_;
  std::unique_ptr<scenic_lib::Buffer> scenic_buffer_;
  vk::DeviceSize size_;
  vk::BufferUsageFlags flags_;
};

}  // namespace sketchy_service
