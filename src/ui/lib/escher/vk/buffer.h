// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_VK_BUFFER_H_
#define SRC_UI_LIB_ESCHER_VK_BUFFER_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/resources/waitable_resource.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"

namespace escher {

class Buffer;
typedef fxl::RefPtr<Buffer> BufferPtr;

// Escher's standard interface to Vulkan buffer objects. Other than subclassing
// Resource, this class only holds onto the various pieces of state. Particular
// subclasses may have custom deletion logic.
class Buffer : public WaitableResource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  // Return the underlying Vulkan buffer object.
  vk::Buffer vk() { return buffer_; }

  // Return the size of the buffer.
  vk::DeviceSize size() const { return size_; }

  // If the buffer is host-accessible, then this returns a direct pointer to
  // cache-coherent device memory.  Otherwise, returns nullptr.
  uint8_t* host_ptr() const { return host_ptr_; }

 protected:
  Buffer(ResourceManager* manager, vk::Buffer buffer, vk::DeviceSize size,
         uint8_t* host_ptr);

 private:
  // Underlying Vulkan buffer object.
  const vk::Buffer buffer_;
  const vk::DeviceSize size_;
  uint8_t* const host_ptr_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_VK_BUFFER_H_
