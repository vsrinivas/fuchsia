// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/buffer.h"
#include "escher/vk/gpu_mem.h"

namespace sketchy_service {

static scenic_lib::Buffer NewScenicBufferFromEscherBuffer(
    const escher::BufferPtr& buffer,
    scenic_lib::Session* session) {
  auto result = buffer->device().exportMemoryMAGMA(buffer->mem()->base());
  FXL_CHECK(result.result == vk::Result::eSuccess);

  scenic_lib::Memory memory(session, mx::vmo(result.value),
                            scenic::MemoryType::VK_DEVICE_MEMORY);

  return scenic_lib::Buffer(memory, 0, buffer->size());
}

Buffer::Buffer(scenic_lib::Session* session,
               escher::BufferFactory* factory,
               vk::DeviceSize size)
    : Buffer(session,
             factory->NewBuffer(size,
                                vk::BufferUsageFlagBits::eVertexBuffer |
                                    vk::BufferUsageFlagBits::eStorageBuffer |
                                    vk::BufferUsageFlagBits::eTransferDst,
                                vk::MemoryPropertyFlagBits::eDeviceLocal)) {}

Buffer::Buffer(scenic_lib::Session* session, escher::BufferPtr buffer)
    : escher_buffer_(std::move(buffer)),
      scenic_buffer_(NewScenicBufferFromEscherBuffer(escher_buffer_, session)) {
}

}  // namespace sketchy_service
