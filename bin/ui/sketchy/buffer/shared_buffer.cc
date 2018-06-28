// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/buffer/shared_buffer.h"

#include "garnet/bin/ui/sketchy/buffer/shared_buffer_pool.h"
#include "garnet/bin/ui/sketchy/frame.h"
#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer_pool.h"
#include "lib/escher/util/fuchsia_utils.h"
#include "lib/escher/vk/gpu_mem.h"

namespace {

const vk::BufferUsageFlags kBufferUsageFlags =
    vk::BufferUsageFlagBits::eVertexBuffer |
    vk::BufferUsageFlagBits::eIndexBuffer |
    vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eTransferSrc |
    vk::BufferUsageFlagBits::eTransferDst;

const vk::MemoryPropertyFlags kMemoryPropertyFlags =
    vk::MemoryPropertyFlagBits::eDeviceLocal;

std::unique_ptr<scenic::Buffer> NewScenicBufferFromEscherBuffer(
    const escher::BufferPtr& buffer, scenic::Session* session) {
  zx::vmo vmo = escher::ExportMemoryAsVmo(buffer->escher(), buffer->mem());

  scenic::Memory memory(session, std::move(vmo),
                            fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  return std::make_unique<scenic::Buffer>(memory, 0, buffer->size());
}

}  // namespace

namespace sketchy_service {

SharedBufferPtr SharedBuffer::New(scenic::Session* session,
                                  escher::BufferFactory* factory,
                                  vk::DeviceSize capacity) {
  return fxl::AdoptRef(new SharedBuffer(session, factory, capacity));
}

SharedBuffer::SharedBuffer(scenic::Session* session,
                           escher::BufferFactory* factory,
                           vk::DeviceSize capacity)
    : session_(session),
      escher_buffer_(factory->NewBuffer(capacity, kBufferUsageFlags,
                                        kMemoryPropertyFlags)),
      scenic_buffer_(NewScenicBufferFromEscherBuffer(escher_buffer_, session)) {
}

escher::BufferRange SharedBuffer::Reserve(vk::DeviceSize size) {
  FXL_DCHECK(size_ + size <= capacity());
  auto old_size = size_;
  size_ += size;
  return {old_size, size};
}

void SharedBuffer::Copy(Frame* frame, const SharedBufferPtr& from) {
  FXL_DCHECK(from->size() <= capacity());
  frame->command()->CopyBufferAfterBarrier(
      from->escher_buffer(), escher_buffer_, {0, 0, from->size()},
      vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eShaderWrite,
      vk::PipelineStageFlagBits::eTransfer |
          vk::PipelineStageFlagBits::eComputeShader);
  size_ = from->size();
}

void SharedBuffer::Reset() { size_ = 0; }

}  // namespace sketchy_service
