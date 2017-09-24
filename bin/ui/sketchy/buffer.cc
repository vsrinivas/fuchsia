// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/buffer.h"
#include "escher/escher.h"
#include "escher/impl/command_buffer_pool.h"
#include "escher/vk/gpu_mem.h"

namespace {

const vk::BufferUsageFlags kVertexBufferUsageFlags =
    vk::BufferUsageFlagBits::eVertexBuffer |
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferSrc |
        vk::BufferUsageFlagBits::eTransferDst;

const vk::BufferUsageFlags kIndexBufferUsageFlags =
    vk::BufferUsageFlagBits::eIndexBuffer |
        vk::BufferUsageFlagBits::eTransferSrc |
        vk::BufferUsageFlagBits::eTransferDst;

const vk::MemoryPropertyFlags kMemoryPropertyFlags =
    vk::MemoryPropertyFlagBits::eDeviceLocal;

std::unique_ptr<scenic_lib::Buffer> NewScenicBufferFromEscherBuffer(
    const escher::BufferPtr& buffer,
    scenic_lib::Session* session) {
  auto result = buffer->device().exportMemoryMAGMA(buffer->mem()->base());
  FXL_CHECK(result.result == vk::Result::eSuccess);

  scenic_lib::Memory memory(session, zx::vmo(result.value),
                            scenic::MemoryType::VK_DEVICE_MEMORY);
  return std::make_unique<scenic_lib::Buffer>(memory, 0, buffer->size());
}

}  // namespace

namespace sketchy_service {

std::unique_ptr<Buffer> Buffer::New(scenic_lib::Session* session,
                                    escher::BufferFactory* factory,
                                    BufferType type,
                                    vk::DeviceSize capacity) {
  return std::make_unique<Buffer>(
      session,
      factory,
      capacity,
      type == BufferType::kVertex ?
          kVertexBufferUsageFlags : kIndexBufferUsageFlags);
}

Buffer::Buffer(scenic_lib::Session* session,
               escher::BufferFactory* factory,
               vk::DeviceSize capacity,
               vk::BufferUsageFlags flags)
    : session_(session),
      escher_buffer_(factory->NewBuffer(capacity, flags, kMemoryPropertyFlags)),
      scenic_buffer_(NewScenicBufferFromEscherBuffer(escher_buffer_, session)),
      size_(0),
      flags_(flags) {}

void Buffer::Merge(escher::impl::CommandBuffer* command,
                   escher::BufferFactory* factory,
                   escher::BufferPtr new_escher_buffer) {
  // Determine the capacity required to merge the new buffer with the existing
  // one.
  vk::DeviceSize new_capacity = capacity();
  while (size_ + new_escher_buffer->size() > new_capacity) {
    new_capacity <<= 1;
  }

  // If there was not enough capacity, a new buffer must be allocated, and the
  // previously-existing data must be copied into it.
  if (new_capacity > capacity()) {
    auto expanded_escher_buffer = factory->NewBuffer(
        new_capacity, flags_, kMemoryPropertyFlags);
    command->CopyBuffer(escher_buffer_, expanded_escher_buffer, {0, 0, size_});

    escher_buffer_ = std::move(expanded_escher_buffer);
    scenic_buffer_ = NewScenicBufferFromEscherBuffer(escher_buffer_, session_);
  }

  command->CopyBuffer(new_escher_buffer, escher_buffer_,
                      {0, size_, new_escher_buffer->size()});
  size_ += new_escher_buffer->size();
}

}  // namespace sketchy_service
