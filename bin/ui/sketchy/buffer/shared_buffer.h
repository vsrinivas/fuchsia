// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_BUFFER_SHARED_BUFFER_H_
#define GARNET_BIN_UI_SKETCHY_BUFFER_SHARED_BUFFER_H_

#include "lib/escher/vk/buffer_factory.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

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
  static SharedBufferPtr New(scenic::Session* session,
                             escher::BufferFactory* factory,
                             vk::DeviceSize capacity);

  // Reserve a chunk of |size| for use. The requested |size| MUST fit in the
  // remaining unused space in the buffer.  Return the range in the buffer
  // that may be used by the caller; it is unsafe to use anything outside
  // this range (unless the caller somehow knows about the previously-reserved
  // ranges).
  escher::BufferRange Reserve(vk::DeviceSize size);

  // Discard the original content, and copy the content from the other one.
  void Copy(Frame* frame, const SharedBufferPtr& from);

  // Reset the buffer to unused state.
  void Reset();

  const escher::BufferPtr& escher_buffer() const { return escher_buffer_; }
  const scenic::Buffer& scenic_buffer() const { return *scenic_buffer_.get(); }
  vk::DeviceSize capacity() const { return escher_buffer_->size(); }
  vk::DeviceSize size() const { return size_; }

 private:
  friend class SharedBufferPool;

  SharedBuffer(scenic::Session* session, escher::BufferFactory* factory,
               vk::DeviceSize capacity);

  scenic::Session* const session_;
  escher::BufferPtr escher_buffer_;
  std::unique_ptr<scenic::Buffer> scenic_buffer_;
  vk::DeviceSize size_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(SharedBuffer);
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_BUFFER_SHARED_BUFFER_H_
