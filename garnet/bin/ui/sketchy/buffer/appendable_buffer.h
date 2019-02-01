// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_BUFFER_APPENDABLE_BUFFER_H_
#define GARNET_BIN_UI_SKETCHY_BUFFER_APPENDABLE_BUFFER_H_

#include "garnet/public/lib/escher/vk/buffer.h"
#include "garnet/public/lib/escher/vk/buffer_factory.h"

namespace sketchy_service {

// Wraps around an escher buffer for storage. Grows on demand.
class AppendableBuffer {
 public:
  explicit AppendableBuffer(escher::BufferFactory* factory);
  // Replace the current contents of the buffer with |data|. If the existing
  // capacity is insufficient, allocate a new buffer first.
  void SetData(escher::impl::CommandBuffer* command,
               escher::BufferFactory* factory, const void* data, size_t size);
  // Append contents of |data| to the buffer. If the existing capacity is
  // insufficient, first copy the existing data to a new buffer that is large
  // enough, then append to that.
  void AppendData(escher::impl::CommandBuffer* command,
                  escher::BufferFactory* factory, const void* data, size_t size,
                  bool is_stable);

  const escher::BufferPtr& get() const { return buffer_; }
  vk::DeviceSize size() const { return size_; }
  vk::DeviceSize capacity() const { return buffer_->size(); }

 private:
  escher::BufferPtr buffer_;
  vk::DeviceSize size_ = 0;
  vk::DeviceSize stable_size_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(AppendableBuffer);
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_BUFFER_APPENDABLE_BUFFER_H_
