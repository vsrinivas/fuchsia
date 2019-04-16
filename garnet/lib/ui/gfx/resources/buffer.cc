// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/buffer.h"

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/public/lib/escher/impl/naive_buffer.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Buffer::kTypeInfo = {ResourceType::kBuffer, "Buffer"};

Buffer::Buffer(Session* session, ResourceId id, escher::GpuMemPtr gpu_mem,
               ResourcePtr backing_resource)
    : Resource(session, id, Buffer::kTypeInfo),
      backing_resource_(std::move(backing_resource)),
      escher_buffer_(escher::impl::NaiveBuffer::New(
          session->resource_context().escher_resource_recycler,
          std::move(gpu_mem),
          // TODO(SCN-1369): Clients have no way to know this set of bits, and
          // yet our code assumes that the imported VMO will bind successfully.
          vk::BufferUsageFlagBits::eTransferSrc |
              vk::BufferUsageFlagBits::eTransferDst |
              vk::BufferUsageFlagBits::eStorageTexelBuffer |
              vk::BufferUsageFlagBits::eStorageBuffer |
              vk::BufferUsageFlagBits::eIndexBuffer |
              vk::BufferUsageFlagBits::eVertexBuffer)) {}

}  // namespace gfx
}  // namespace scenic_impl
