// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_NAIVE_BUFFER_H_
#define LIB_ESCHER_IMPL_NAIVE_BUFFER_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/resources/waitable_resource.h"
#include "lib/escher/vk/buffer.h"
#include "lib/escher/vk/gpu_mem.h"

namespace escher {
namespace impl {

// This particular class takes ownership of the contained vk::Buffer, and
// destroys it using vk::Device::destroyBuffer when it goes out of scope. It
// also automatically binds the buffer to the entirety of the submitted memory
// object.
class NaiveBuffer : public Buffer {
 public:
  static BufferPtr New(ResourceManager* manager, GpuMemPtr mem,
                       vk::BufferUsageFlags usage_flags);

  NaiveBuffer(ResourceManager* manager, GpuMemPtr mem, vk::Buffer buffer);
  ~NaiveBuffer() override;

 private:
  // Backing memory object.
  GpuMemPtr mem_;
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_NAIVE_BUFFER_H_
