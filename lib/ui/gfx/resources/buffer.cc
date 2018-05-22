// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/buffer.h"

#include "garnet/lib/ui/gfx/engine/session.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo Buffer::kTypeInfo = {ResourceType::kBuffer, "Buffer"};

Buffer::Buffer(Session* session, scenic::ResourceId id, GpuMemoryPtr memory,
               uint32_t size, uint32_t offset)
    : Resource(session, id, Buffer::kTypeInfo),
      memory_(std::move(memory)),
      escher_buffer_(escher::Buffer::New(
          session->escher()->resource_recycler(), memory_->escher_gpu_mem(),
          vk::BufferUsageFlagBits::eTransferSrc |
              vk::BufferUsageFlagBits::eTransferDst |
              vk::BufferUsageFlagBits::eStorageTexelBuffer |
              vk::BufferUsageFlagBits::eStorageBuffer |
              vk::BufferUsageFlagBits::eIndexBuffer |
              vk::BufferUsageFlagBits::eVertexBuffer,
          size, offset)) {}

}  // namespace gfx
}  // namespace scenic
