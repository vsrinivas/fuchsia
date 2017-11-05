// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/buffer.h"
#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/util/fuchsia_utils.h"
#include "lib/escher/vk/gpu_mem.h"

namespace {

const vk::BufferUsageFlags kVertexBufferUsageFlags =
    vk::BufferUsageFlagBits::eVertexBuffer |
    vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eTransferSrc |
    vk::BufferUsageFlagBits::eTransferDst;

const vk::BufferUsageFlags kIndexBufferUsageFlags =
    vk::BufferUsageFlagBits::eIndexBuffer |
    vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eTransferSrc |
    vk::BufferUsageFlagBits::eTransferDst;

const vk::MemoryPropertyFlags kMemoryPropertyFlags =
    vk::MemoryPropertyFlagBits::eDeviceLocal;

std::unique_ptr<scenic_lib::Buffer> NewScenicBufferFromEscherBuffer(
    const escher::BufferPtr& buffer,
    scenic_lib::Session* session) {
  zx::vmo vmo =
      escher::ExportMemoryAsVmo(buffer->escher(), buffer->mem());

  scenic_lib::Memory memory(session, std::move(vmo),
                            scenic::MemoryType::VK_DEVICE_MEMORY);
  return std::make_unique<scenic_lib::Buffer>(memory, 0, buffer->size());
}

}  // namespace

namespace sketchy_service {

std::unique_ptr<Buffer> Buffer::New(scenic_lib::Session* session,
                                    escher::BufferFactory* factory,
                                    BufferType type,
                                    vk::DeviceSize capacity) {
  return std::make_unique<Buffer>(session, factory, capacity,
                                  type == BufferType::kVertex
                                      ? kVertexBufferUsageFlags
                                      : kIndexBufferUsageFlags);
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
  PreserveSize(command, factory, new_escher_buffer->size());
  command->CopyBuffer(new_escher_buffer, escher_buffer_,
                      {0, size_ - new_escher_buffer->size(),
                       new_escher_buffer->size()});
}

escher::BufferPtr Buffer::PreserveBuffer(escher::impl::CommandBuffer* command,
                                         escher::BufferFactory* factory,
                                         vk::DeviceSize size) {
  PreserveSize(command, factory, size);
  return factory->NewBuffer(escher_buffer_->mem(), flags_, size, size_ - size);
}

void Buffer::PreserveSize(escher::impl::CommandBuffer* command,
                          escher::BufferFactory* factory,
                          vk::DeviceSize size) {
  // Determine the capacity required to preserve for |size|.
  vk::DeviceSize new_capacity = capacity();
  while (size_ + size > new_capacity) {
    new_capacity <<= 1;
  }

  // If there was not enough capacity, a new buffer must be allocated, and the
  // previously-existing data must be copied into it.
  if (new_capacity > capacity()) {
    auto expanded_escher_buffer =
        factory->NewBuffer(new_capacity, flags_, kMemoryPropertyFlags);
    command->CopyBufferAfterBarrier(
        escher_buffer_, expanded_escher_buffer, {0, 0, size_},
        vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eShaderWrite);
    escher_buffer_ = std::move(expanded_escher_buffer);
    scenic_buffer_ = NewScenicBufferFromEscherBuffer(escher_buffer_, session_);
  }

  // Preserve the size for use.
  size_ += size;
}

}  // namespace sketchy_service
