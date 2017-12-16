// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/buffer/escher_buffer.h"

#include "lib/escher/impl/command_buffer.h"

namespace {

constexpr uint32_t kDefaultPower = 10;
constexpr vk::DeviceSize kDefaultCapacity = 1U << kDefaultPower;

const vk::BufferUsageFlags kBufferUsageFlags =
    vk::BufferUsageFlagBits::eVertexBuffer |
        vk::BufferUsageFlagBits::eIndexBuffer |
        vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferSrc |
        vk::BufferUsageFlagBits::eTransferDst;

const vk::MemoryPropertyFlags kMemoryPropertyFlags =
    vk::MemoryPropertyFlagBits::eDeviceLocal;

vk::DeviceSize GetCapacity(vk::DeviceSize capacity_req) {
  capacity_req = std::max(1UL, capacity_req);
  auto power = static_cast<uint32_t>(std::ceil(std::log2(capacity_req)));
  return 1U << std::max(kDefaultPower, power);
}

// TODO(MZ-269): Implement a staging buffer pool.
escher::BufferPtr GetStagingBuffer(escher::BufferFactory* factory,
                                   vk::DeviceSize capacity_req) {
  return factory->NewBuffer(
      capacity_req,
      vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent);
}

}  // namespace

namespace sketchy_service {

EscherBuffer::EscherBuffer(escher::BufferFactory* factory)
    : buffer_(
        factory->NewBuffer(
            kDefaultCapacity, kBufferUsageFlags, kMemoryPropertyFlags)) {}

void EscherBuffer::SetData(escher::impl::CommandBuffer* command,
                           escher::BufferFactory* factory,
                           const void* data, size_t size) {
  if (size == 0) {
    size_ = 0;
    return;
  }
  if (size > capacity()) {
    buffer_ = factory->NewBuffer(
        GetCapacity(size), kBufferUsageFlags, kMemoryPropertyFlags);
  }
  auto staging_buffer = GetStagingBuffer(factory, size);
  memcpy(staging_buffer->ptr(), data, size);
  command->CopyBuffer(staging_buffer, buffer_, {0, 0, size});
  size_ = size;
}

void EscherBuffer::AppendData(escher::impl::CommandBuffer* command,
                              escher::BufferFactory* factory,
                              const void* data, size_t size) {
  if (size == 0) {
    return;
  }
  size_t total_size = size + size_;
  if (total_size > capacity()) {
    auto new_buffer = factory->NewBuffer(
        GetCapacity(total_size), kBufferUsageFlags, kMemoryPropertyFlags);
    command->CopyBufferAfterBarrier(
        buffer_, new_buffer, {0, 0, size_},
        vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eShaderRead);
    buffer_ = std::move(new_buffer);
  }
  auto staging_buffer = GetStagingBuffer(factory, size);
  memcpy(staging_buffer->ptr(), data, size);
  command->CopyBuffer(staging_buffer, buffer_, {0, size_, size});
  size_ = total_size;
}

}  // namespace sketchy_service
