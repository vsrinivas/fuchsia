// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/resources/resource_recycler.h"
#include "escher/vk/buffer.h"

namespace escher {

// BufferFactory allows clients to obtain unused Buffers with the desired
// properties.  The default implementation allocates memory and creates a new
// Buffer, but subclasses may override this behavior, e.g. to support efficient
// recycling of fixed-size Buffers.
class BufferFactory : private ResourceRecycler {
 public:
  explicit BufferFactory(Escher* escher);

  virtual BufferPtr NewBuffer(vk::DeviceSize size,
                              vk::BufferUsageFlags usage_flags,
                              vk::MemoryPropertyFlags memory_property_flags);

  // Expose escher()... this is one aspect of ResourceRecycler that we want to
  // inherit publicly.
  using ResourceRecycler::escher;
};

}  // namespace escher
