// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/uniform_buffer_pool.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/vk/gpu_allocator.h"

namespace escher {
namespace impl {

// TODO: obtain max uniform-buffer size from Vulkan.  64kB is typical.
constexpr vk::DeviceSize kBufferSize = 65536;

UniformBufferPool::UniformBufferPool(EscherWeakPtr escher,
                                     GpuAllocator* allocator,
                                     vk::MemoryPropertyFlags additional_flags)
    : ResourceManager(escher),
      allocator_(allocator ? allocator : escher->gpu_allocator()),
      flags_(additional_flags | vk::MemoryPropertyFlagBits::eHostVisible),
      buffer_size_(kBufferSize) {}

UniformBufferPool::~UniformBufferPool() {}

BufferPtr UniformBufferPool::Allocate() {
  if (free_buffers_.empty()) {
    InternalAllocate();
  }
  BufferPtr buf(free_buffers_.back().release());
  free_buffers_.pop_back();
  return buf;
}

void UniformBufferPool::InternalAllocate() {
  // Create a batch of buffers.
  constexpr uint32_t kBufferBatchSize = 10;
  vk::Buffer new_buffers[kBufferBatchSize];
  vk::BufferCreateInfo info;
  info.size = buffer_size_;
  info.usage = vk::BufferUsageFlagBits::eUniformBuffer;
  info.sharingMode = vk::SharingMode::eExclusive;
  for (uint32_t i = 0; i < kBufferBatchSize; ++i) {
    new_buffers[i] = ESCHER_CHECKED_VK_RESULT(vk_device().createBuffer(info));
  }

  // Determine the memory requirements for a single buffer.
  vk::MemoryRequirements reqs =
      vk_device().getBufferMemoryRequirements(new_buffers[0]);
  // If necessary, we can write the logic to deal with the conditions below.
  FXL_CHECK(buffer_size_ == reqs.size);
  FXL_CHECK(buffer_size_ % reqs.alignment == 0);

  // Allocate enough memory for all of the buffers.
  reqs.size *= kBufferBatchSize;
  auto batch_mem = allocator_->Allocate(reqs, flags_);

  for (uint32_t i = 0; i < kBufferBatchSize; ++i) {
    // Validation layer complains if we bind a buffer to memory without first
    // querying it's memory requirements.  This shouldn't be necessary, since
    // all buffers are identically-configured.
    // TODO: disable this in release mode.
    vk_device().getBufferMemoryRequirements(new_buffers[i]);

    // Sub-allocate memory for each buffer.
    auto mem = batch_mem->Allocate(buffer_size_, i * buffer_size_);

    // Workaround for dealing with RefPtr/Reffable Adopt() semantics.  Let the
    // RefPtr go out of scope immediately; the Buffer will be added to
    // free_buffers_ via OnReceiveOwnable().
    fxl::MakeRefCounted<Buffer>(this, std::move(mem), new_buffers[i],
                                buffer_size_);
  }
}

void UniformBufferPool::OnReceiveOwnable(std::unique_ptr<Resource> resource) {
  FXL_DCHECK(resource->IsKindOf<Buffer>());
  free_buffers_.emplace_back(static_cast<Buffer*>(resource.release()));
}

}  // namespace impl
}  // namespace escher
