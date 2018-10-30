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
    const escher::BufferPtr& buffer, scenic::Session* session,
    const escher::GpuMemPtr& mem) {
  // This code assumes that the VMO extracted from the memory pointer is solely
  // used for the buffer assigned to that memory. Otherwise, this will have the
  // unfortunate side effect of mapping much more memory into the Scenic process
  // than expected.
  //
  // It also assumes the GpuMemPtr passed in is the backing memory for the
  // escher::Buffer object, as we can no longer extract the GpuMemPtr from the
  // escher::Buffer object directly.
  FXL_DCHECK(mem->offset() == 0);
  FXL_DCHECK(mem->size() == buffer->size());
  zx::vmo vmo = escher::ExportMemoryAsVmo(buffer->escher(), mem);
  scenic::Memory memory(session, std::move(vmo), mem->size(),
                        fuchsia::images::MemoryType::VK_DEVICE_MEMORY);
  return std::make_unique<scenic::Buffer>(memory, mem->offset(), mem->size());
}

}  // namespace

namespace sketchy_service {

SharedBufferPtr SharedBuffer::New(scenic::Session* session,
                                  escher::ResourceRecycler* recycler,
                                  vk::DeviceSize capacity) {
  return fxl::AdoptRef(new SharedBuffer(session, recycler, capacity));
}

SharedBuffer::SharedBuffer(scenic::Session* session,
                           escher::ResourceRecycler* recycler,
                           vk::DeviceSize capacity) {
  // Normally, this code might use a buffer factory or a gpu_allocator
  // to construct its buffer. However, if we did that, then we might not have a
  // unique VMO just for this one resource. Instead, we might get a VMO for a
  // managed pool or a circular buffer. Therefore, we go through the steps here
  // to create our own fresh slab, to make sure that the VMO is isolated in its
  // contents.
  escher::GpuMemPtr mem;
  escher_buffer_ =
      escher::Buffer::New(recycler, nullptr, capacity, kBufferUsageFlags,
                          kMemoryPropertyFlags, &mem);
  scenic_buffer_ =
      NewScenicBufferFromEscherBuffer(escher_buffer_, session, mem);
}

escher::impl::BufferRange SharedBuffer::Reserve(vk::DeviceSize size) {
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
