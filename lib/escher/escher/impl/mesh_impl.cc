// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/mesh_impl.h"

#include "escher/impl/mesh_manager.h"

namespace escher {
namespace impl {

MeshImpl::MeshImpl(MeshSpec spec,
                   uint32_t num_vertices,
                   uint32_t num_indices,
                   MeshManager* manager,
                   Buffer vertex_buffer,
                   Buffer index_buffer,
                   vk::Semaphore mesh_ready_semaphore)
    : Mesh(spec, num_vertices, num_indices),
      manager_(manager),
      vertex_buffer_(std::move(vertex_buffer)),
      index_buffer_(std::move(index_buffer)),
      mesh_ready_semaphore_(mesh_ready_semaphore) {}

MeshImpl::~MeshImpl() {
  // Need to schedule destruction of buffers once the last frame that uses the
  // mesh is finished.
  manager_->DestroyMeshResources(
      last_rendered_frame_, std::move(vertex_buffer_), std::move(index_buffer_),
      mesh_ready_semaphore_);
}

vk::Semaphore MeshImpl::Draw(vk::CommandBuffer command_buffer,
                             uint64_t frame_number) {
  FTL_DCHECK(frame_number >= last_rendered_frame_);
  last_rendered_frame_ = frame_number;

  vk::Buffer vbo = vertex_buffer_.buffer();
  // TODO: offset won't be zero in general.
  vk::DeviceSize offset = 0;
  command_buffer.bindVertexBuffers(spec.GetVertexBinding(), 1, &vbo, &offset);
  command_buffer.bindIndexBuffer(index_buffer_.buffer(), offset,
                                 vk::IndexType::eUint32);
  command_buffer.drawIndexed(num_indices, 1, 0, 0, 0);

  if (mesh_ready_semaphore_) {
    vk::Semaphore saved = mesh_ready_semaphore_;
    mesh_ready_semaphore_ = nullptr;
    return saved;
  }
  return vk::Semaphore();
}

}  // namespace impl
}  // namespace escher
