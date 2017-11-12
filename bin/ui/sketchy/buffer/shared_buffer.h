// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/vk/buffer_factory.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"

namespace sketchy_service {

class Frame;
class SharedBufferPool;

class SharedBuffer;
using SharedBufferPtr = fxl::RefPtr<SharedBuffer>;

// Encapsulates an Escher buffer and a Scenic buffer, which share the
// same memory.  The Escher buffer is exported as a VMO, which is used to
// create the Scenic buffer.
class SharedBuffer final : public fxl::RefCountedThreadSafe<SharedBuffer> {
 public:
  static SharedBufferPtr New(scenic_lib::Session* session,
                             escher::BufferFactory* factory,
                             vk::DeviceSize capacity);

  // Preserve a chunk of |size| for use. The requested |size| MUST fit in the
  // rest of the buffer.
  escher::BufferPtr Preserve(Frame* frame, vk::DeviceSize size);

  // Discard the original content, and copy the content from the other one.
  void Copy(Frame* frame, const SharedBufferPtr& from);

  // Reset the buffer to unused state.
  void Reset();

  const escher::BufferPtr& escher_buffer() const { return escher_buffer_; }
  const scenic_lib::Buffer& scenic_buffer() const {
    return *scenic_buffer_.get();
  }
  vk::DeviceSize capacity() const { return escher_buffer_->size(); }
  vk::DeviceSize size() const { return size_; }
  bool released_by_canvas() const { return released_by_canvas_; }
  bool released_by_scenic() const { return released_by_scenic_; }

 private:
  friend class SharedBufferPool;

  SharedBuffer(scenic_lib::Session* session,
               escher::BufferFactory* factory,
               vk::DeviceSize capacity);

  scenic_lib::Session* const session_;
  escher::BufferPtr escher_buffer_;
  std::unique_ptr<scenic_lib::Buffer> scenic_buffer_;
  vk::DeviceSize size_ = 0;

  bool released_by_canvas_ = false;
  bool released_by_scenic_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(SharedBuffer);
};

}  // namespace sketchy_service
