// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"
#include "escher/vk/buffer_factory.h"

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
  static std::unique_ptr<Buffer> New(
      scenic_lib::Session* session,
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

  const escher::BufferPtr& escher_buffer() const { return escher_buffer_; }
  const scenic_lib::Buffer& scenic_buffer() const {
    return *scenic_buffer_.get();
  }
  vk::DeviceSize capacity() const { return escher_buffer_->size(); }
  vk::DeviceSize size() const { return size_; }

 private:
  scenic_lib::Session* const session_;
  escher::BufferPtr escher_buffer_;
  std::unique_ptr<scenic_lib::Buffer> scenic_buffer_;
  vk::DeviceSize size_;
  vk::BufferUsageFlags flags_;
};

}  // namespace sketchy_service
