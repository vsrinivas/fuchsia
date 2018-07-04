// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_BUFFER_FACTORY_H_
#define LIB_ESCHER_VK_BUFFER_FACTORY_H_

#include "lib/escher/resources/resource_recycler.h"
#include "lib/escher/vk/buffer.h"

namespace escher {

// BufferFactory allows clients to obtain unused Buffers with the desired
// properties.  The default implementation allocates memory and creates a new
// Buffer, but subclasses may override this behavior, e.g. to support efficient
// recycling of fixed-size Buffers.
class BufferFactory : private ResourceRecycler {
 public:
  explicit BufferFactory(EscherWeakPtr escher);

  // Creates a buffer, along with a new memory.
  virtual BufferPtr NewBuffer(vk::DeviceSize size,
                              vk::BufferUsageFlags usage_flags,
                              vk::MemoryPropertyFlags memory_property_flags);

  // Creates a buffer for the given memory.
  virtual BufferPtr NewBuffer(GpuMemPtr mem, vk::BufferUsageFlags usage_flags,
                              vk::DeviceSize size, vk::DeviceSize offset);

  // Expose escher()... this is one aspect of ResourceRecycler that we want to
  // inherit publicly.
  using ResourceRecycler::escher;
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_BUFFER_FACTORY_H_
