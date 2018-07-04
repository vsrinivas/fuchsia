// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/buffer_factory.h"
#include "lib/escher/vk/gpu_mem.h"

#include "lib/escher/escher.h"

namespace escher {

BufferFactory::BufferFactory(EscherWeakPtr escher)
    : ResourceRecycler(std::move(escher)) {}

BufferPtr BufferFactory::NewBuffer(
    vk::DeviceSize size, vk::BufferUsageFlags usage_flags,
    vk::MemoryPropertyFlags memory_property_flags) {
  return Buffer::New(this, escher()->gpu_allocator(), size, usage_flags,
                     memory_property_flags);
}

BufferPtr BufferFactory::NewBuffer(GpuMemPtr mem,
                                   vk::BufferUsageFlags usage_flags,
                                   vk::DeviceSize size, vk::DeviceSize offset) {
  return Buffer::New(this, mem, usage_flags, size, offset);
}

}  // namespace escher
