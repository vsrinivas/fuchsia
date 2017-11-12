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

SharedBufferPtr SharedBuffer::New(scenic_lib::Session* session,
                                  escher::BufferFactory* factory,
                                  vk::DeviceSize capacity) {
  return fxl::AdoptRef(new SharedBuffer(session, factory, capacity));
}

SharedBuffer::SharedBuffer(scenic_lib::Session* session,
                           escher::BufferFactory* factory,
                           vk::DeviceSize capacity)
    : session_(session),
      escher_buffer_(
          factory->NewBuffer(
              capacity, kBufferUsageFlags, kMemoryPropertyFlags)),
      scenic_buffer_(
          NewScenicBufferFromEscherBuffer(escher_buffer_, session)) {}

escher::BufferPtr SharedBuffer::Preserve(Frame* frame,
                                         vk::DeviceSize size) {
  FXL_CHECK(size_ + size <= capacity());
  size_ += size;
  auto factory = frame->shared_buffer_pool()->factory();
  return factory->NewBuffer(
      escher_buffer_->mem(), kBufferUsageFlags, size, size_ - size);
}

void SharedBuffer::Copy(Frame* frame, const SharedBufferPtr& from) {
  FXL_CHECK(from->size() <= capacity());
  frame->command()->CopyBufferAfterBarrier(
      from->escher_buffer(), escher_buffer_,
      {0, 0, from->size()},
      vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eShaderWrite);
  size_ = from->size();
}

void SharedBuffer::Reset() {
  size_ = 0;
  released_by_canvas_ = false;
  released_by_scenic_ = false;
}

}  // namespace sketchy_service
