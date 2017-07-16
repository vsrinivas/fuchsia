// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/vk/buffer_factory.h"

#include "escher/escher.h"

namespace escher {

BufferFactory::BufferFactory(Escher* escher) : ResourceRecycler(escher) {}

BufferPtr BufferFactory::NewBuffer(
    vk::DeviceSize size,
    vk::BufferUsageFlags usage_flags,
    vk::MemoryPropertyFlags memory_property_flags) {
  return Buffer::New(this, escher()->gpu_allocator(), size, usage_flags,
                     memory_property_flags);
}

}  // namespace escher
