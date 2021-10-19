// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/buffer.h"

#include "src/ui/lib/escher/impl/naive_buffer.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Buffer::kTypeInfo = {ResourceType::kBuffer, "Buffer"};

const vk::BufferUsageFlags kBufferUsageFlags =
    vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst |
    vk::BufferUsageFlagBits::eStorageTexelBuffer | vk::BufferUsageFlagBits::eStorageBuffer |
    vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eVertexBuffer;

Buffer::Buffer(Session* session, ResourceId id, escher::GpuMemPtr gpu_mem,
               ResourcePtr backing_resource, std::optional<vk::DeviceSize> size)
    : Resource(session, session->id(), id, Buffer::kTypeInfo),
      backing_resource_(std::move(backing_resource)),
      escher_buffer_(escher::impl::NaiveBuffer::New(
          session->resource_context().escher_resource_recycler, std::move(gpu_mem),
          // TODO(fxbug.dev/24563): Clients have no way to know this set of bits, and
          // yet our code assumes that the imported VMO will bind successfully.
          kBufferUsageFlags, size)) {}

// static
vk::MemoryRequirements Buffer::GetMemoryRequirements(Session* session,
                                                     vk::DeviceSize size_requested) {
  auto vk_device = session->resource_context().escher_resource_recycler->vk_device();
  vk::BufferCreateInfo info;
  info.setUsage(kBufferUsageFlags).setSize(size_requested);

  // Create a temporary VkBuffer and destroy it on function return.
  auto buffer_result = vk_device.createBufferUnique(info);
  FX_DCHECK(buffer_result.result == vk::Result::eSuccess);

  auto buffer = std::move(buffer_result.value);
  auto reqs = vk_device.getBufferMemoryRequirements(*buffer);
  return reqs;
}

}  // namespace gfx
}  // namespace scenic_impl
