// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/shape/rounded_rect_factory.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/gpu_uploader.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/shape/mesh_spec.h"
#include "lib/escher/vk/buffer_factory.h"

namespace escher {

RoundedRectFactory::RoundedRectFactory(EscherWeakPtr weak_escher)
    : ResourceRecycler(std::move(weak_escher)),
      buffer_factory_(std::make_unique<BufferFactory>(GetEscherWeakPtr())),
      uploader_(escher()->gpu_uploader()) {}

RoundedRectFactory::~RoundedRectFactory() {}

MeshPtr RoundedRectFactory::NewRoundedRect(const RoundedRectSpec& spec,
                                           const MeshSpec& mesh_spec) {
  auto index_buffer = GetIndexBuffer(spec, mesh_spec);

  auto counts = GetRoundedRectMeshVertexAndIndexCounts(spec);
  uint32_t vertex_count = counts.first;
  uint32_t index_count = counts.second;
  size_t vertex_buffer_size = vertex_count * mesh_spec.GetStride();

  auto vertex_buffer = buffer_factory_->NewBuffer(
      vertex_buffer_size, vk::BufferUsageFlagBits::eVertexBuffer |
                              vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  impl::GpuUploader::Writer writer = uploader_->GetWriter(vertex_buffer_size);
  GenerateRoundedRectVertices(spec, mesh_spec, writer.ptr(), writer.size());
  writer.WriteBuffer(vertex_buffer, {0, 0, vertex_buffer->size()},
                     Semaphore::New(vk_device()));
  writer.Submit();

  BoundingBox bounding_box =
      spec.width > 0.f && spec.height > 0.f
          ? BoundingBox(-0.5f * vec3(spec.width, spec.height, 0),
                        0.5f * vec3(spec.width, spec.height, 0))
          : BoundingBox();

  return fxl::MakeRefCounted<Mesh>(
      static_cast<ResourceRecycler*>(this), mesh_spec, bounding_box,
      vertex_count, index_count, vertex_buffer, std::move(index_buffer));
}

BufferPtr RoundedRectFactory::GetIndexBuffer(const RoundedRectSpec& spec,
                                             const MeshSpec& mesh_spec) {
  // Lazily create index buffer.  Since the rounded-rect tessellation functions
  // don't currently take |RoundedRectSpec.zoom| into account, we can always
  // return the same index buffer.
  if (!index_buffer_) {
    uint32_t index_count = GetRoundedRectMeshVertexAndIndexCounts(spec).second;
    size_t index_buffer_size = index_count * MeshSpec::kIndexSize;

    index_buffer_ = buffer_factory_->NewBuffer(
        index_buffer_size, vk::BufferUsageFlagBits::eIndexBuffer |
                               vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    impl::GpuUploader::Writer writer = uploader_->GetWriter(index_buffer_size);
    GenerateRoundedRectIndices(spec, mesh_spec, writer.ptr(), writer.size());
    writer.WriteBuffer(index_buffer_, {0, 0, index_buffer_->size()},
                       SemaphorePtr());
    writer.Submit();
  }
  return index_buffer_;
}

}  // namespace escher
