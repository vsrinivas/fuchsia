// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/shape/rounded_rect_factory.h"

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/shape/mesh_spec.h"
#include "src/ui/lib/escher/vk/buffer_factory.h"

namespace escher {

RoundedRectFactory::RoundedRectFactory(EscherWeakPtr weak_escher)
    : ResourceRecycler(std::move(weak_escher)),
      buffer_factory_(GetEscherWeakPtr()->gpu_allocator(),
                      GetEscherWeakPtr()->resource_recycler()) {}

RoundedRectFactory::~RoundedRectFactory() {}

MeshPtr RoundedRectFactory::NewRoundedRect(const RoundedRectSpec& spec, const MeshSpec& mesh_spec,
                                           BatchGpuUploader* batch_gpu_uploader) {
  FXL_DCHECK(batch_gpu_uploader);

  auto index_buffer = GetIndexBuffer(spec, mesh_spec, batch_gpu_uploader);

  auto counts = GetRoundedRectMeshVertexAndIndexCounts(spec);
  const uint32_t vertex_count = counts.first;
  const uint32_t index_count = counts.second;
  const size_t primary_buffer_stride = mesh_spec.stride(0);
  const size_t secondary_buffer_stride = mesh_spec.stride(1);

  const size_t vertex_buffer_size =
      vertex_count * (primary_buffer_stride + secondary_buffer_stride);

  auto vertex_buffer = buffer_factory_.NewBuffer(vertex_buffer_size,
                                                 vk::BufferUsageFlagBits::eVertexBuffer |
                                                     vk::BufferUsageFlagBits::eTransferSrc |
                                                     vk::BufferUsageFlagBits::eTransferDst,
                                                 vk::MemoryPropertyFlagBits::eDeviceLocal);

  const auto bounding_box = BoundingBox::NewChecked(-0.5f * vec3(spec.width, spec.height, 0),
                                                    0.5f * vec3(spec.width, spec.height, 0), 1);

  switch (mesh_spec.vertex_buffer_count()) {
    case 1: {
      batch_gpu_uploader->ScheduleWriteBuffer(
          vertex_buffer,
          [spec, mesh_spec, vertex_buffer_size](uint8_t* host_ptr, size_t copy_size) {
            GenerateRoundedRectVertices(spec, mesh_spec, host_ptr, vertex_buffer_size);
          },
          /* target_offset */ 0U, /* copy_size */ vertex_buffer_size);

      return fxl::MakeRefCounted<Mesh>(static_cast<ResourceRecycler*>(this), mesh_spec,
                                       bounding_box, vertex_count, index_count, vertex_buffer,
                                       std::move(index_buffer));
    }
    case 2: {
      batch_gpu_uploader->ScheduleWriteBuffer(
          vertex_buffer,
          [spec, mesh_spec, vertex_count, primary_buffer_stride, secondary_buffer_stride](
              uint8_t* host_ptr, size_t copy_size) {
            GenerateRoundedRectVertices(spec, mesh_spec, host_ptr,
                                        vertex_count * primary_buffer_stride,
                                        host_ptr + vertex_count * primary_buffer_stride,
                                        vertex_count * secondary_buffer_stride);
          },
          /* target_offset */ 0U, /* copy_size */ vertex_buffer_size);

      return fxl::MakeRefCounted<Mesh>(static_cast<ResourceRecycler*>(this), mesh_spec,
                                       bounding_box, index_count, std::move(index_buffer), 0,
                                       vertex_count, vertex_buffer, 0, std::move(vertex_buffer),
                                       vertex_count * primary_buffer_stride);
    }
    default:
      FXL_CHECK(false) << "unsupported vertex buffer count: " << mesh_spec.vertex_buffer_count();
      return nullptr;
  }
}

BufferPtr RoundedRectFactory::GetIndexBuffer(const RoundedRectSpec& spec, const MeshSpec& mesh_spec,
                                             BatchGpuUploader* batch_gpu_uploader) {
  FXL_DCHECK(batch_gpu_uploader);
  // Lazily create index buffer.  Since the rounded-rect tessellation functions
  // don't currently take |RoundedRectSpec.zoom| into account, we can always
  // return the same index buffer.
  if (!index_buffer_) {
    uint32_t index_count = GetRoundedRectMeshVertexAndIndexCounts(spec).second;
    size_t index_buffer_size = index_count * sizeof(MeshSpec::IndexType);

    index_buffer_ = buffer_factory_.NewBuffer(index_buffer_size,
                                              vk::BufferUsageFlagBits::eIndexBuffer |
                                                  vk::BufferUsageFlagBits::eTransferSrc |
                                                  vk::BufferUsageFlagBits::eTransferDst,
                                              vk::MemoryPropertyFlagBits::eDeviceLocal);

    batch_gpu_uploader->ScheduleWriteBuffer(
        index_buffer_,
        [spec, mesh_spec, index_buffer_size](uint8_t* host_ptr, size_t copy_size) {
          GenerateRoundedRectIndices(spec, mesh_spec, host_ptr, index_buffer_size);
        },
        /* target_offset */ 0U, /* copy_size */ index_buffer_size);
  }
  return index_buffer_;
}

}  // namespace escher
