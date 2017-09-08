// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/buffer.h"

#include "garnet/bin/ui/scene_manager/engine/session.h"

namespace scene_manager {

const ResourceTypeInfo Buffer::kTypeInfo = {ResourceType::kBuffer, "Buffer"};

Buffer::Buffer(Session* session,
               scenic::ResourceId id,
               GpuMemoryPtr memory,
               uint32_t size,
               uint32_t offset)
    : Resource(session, id, Buffer::kTypeInfo),
      memory_(std::move(memory)),
      escher_buffer_(
          escher::Buffer::New(session->escher()->resource_recycler(),
                              memory_->escher_gpu_mem(),
                              vk::BufferUsageFlagBits::eVertexBuffer |
                                  vk::BufferUsageFlagBits::eStorageBuffer |
                                  vk::BufferUsageFlagBits::eTransferDst,
                              size,
                              offset)) {}

}  // namespace scene_manager
