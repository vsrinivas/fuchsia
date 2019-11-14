// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_IMPL_NAIVE_BUFFER_H_
#define SRC_UI_LIB_ESCHER_IMPL_NAIVE_BUFFER_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/vk/buffer.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"

namespace escher {
namespace impl {

// This particular class takes ownership of the contained vk::Buffer, and
// destroys it using vk::Device::destroyBuffer when it goes out of scope. It
// also automatically binds the buffer to the entirety of the submitted memory
// object.
class NaiveBuffer : public Buffer {
 public:
  // This constructor uses |mem->size()| as its |size_| property.
  static BufferPtr New(ResourceManager* manager, GpuMemPtr mem, vk::BufferUsageFlags usage_flags);

  // This constructor adopts existing VkBuffer and uses |vk_buffer_size| as its
  // |size_| property, which can be different from the size of |mem|.
  static BufferPtr AdoptVkBuffer(ResourceManager* manager, GpuMemPtr mem,
                                 vk::DeviceSize vk_buffer_size, vk::Buffer vk_buffer);

  ~NaiveBuffer() override;

 private:
  // Private constructor. |vk_buffer_size| may be different from the defined
  // size of |buffer|.
  NaiveBuffer(ResourceManager* manager, GpuMemPtr mem, vk::DeviceSize vk_buffer_size,
              vk::Buffer buffer);

  // Backing memory object.
  GpuMemPtr mem_;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_IMPL_NAIVE_BUFFER_H_
