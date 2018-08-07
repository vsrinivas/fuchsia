// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/buffer/mesh_buffer.h"

#include "garnet/bin/ui/sketchy/buffer/shared_buffer_pool.h"
#include "lib/ui/scenic/cpp/commands.h"

namespace {

constexpr vk::DeviceSize kVertexStride = sizeof(float) * 4;
constexpr vk::DeviceSize kIndexStride = sizeof(uint32_t);

constexpr auto kMeshVertexPositionType = fuchsia::ui::gfx::ValueType::kVector2;
constexpr auto kMeshVertexNormalType = fuchsia::ui::gfx::ValueType::kNone;
constexpr auto kMeshVertexTexCoodType = fuchsia::ui::gfx::ValueType::kVector2;
constexpr auto kMeshIndexFormat = fuchsia::ui::gfx::MeshIndexFormat::kUint32;

}  // namespace

namespace sketchy_service {

void MeshBuffer::Prepare(Frame* frame, bool from_scratch,
                         uint32_t delta_vertex_count,
                         uint32_t delta_index_count) {
  from_scratch |= !vertex_buffer_ || !index_buffer_;

  // Multi-buffering for vertex buffer.
  vk::DeviceSize vertex_size = (from_scratch ? 0 : vertex_buffer_->size()) +
                               delta_vertex_count * kVertexStride;
  ReplaceBuffer(frame, vertex_buffer_, vertex_size, !from_scratch);

  // Multi-buffering for index buffer.
  vk::DeviceSize index_size = (from_scratch ? 0 : index_buffer_->size()) +
                              delta_index_count * kIndexStride;
  ReplaceBuffer(frame, index_buffer_, index_size, !from_scratch);

  if (from_scratch) {
    vertex_buffer_->Reset();
    index_buffer_->Reset();
    vertex_count_ = 0;
    index_count_ = 0;
    bbox_ = escher::BoundingBox();
  }
}

std::pair<escher::BufferRange, escher::BufferRange> MeshBuffer::Reserve(
    Frame* frame, uint32_t vertex_count, uint32_t index_count,
    const escher::BoundingBox& bbox) {
  vertex_count_ += vertex_count;
  index_count_ += index_count;
  bbox_.Join(bbox);

  vk::DeviceSize vertex_size = kVertexStride * vertex_count;
  vk::DeviceSize total_vertex_size = vertex_buffer_->size() + vertex_size;
  if (vertex_buffer_->capacity() < total_vertex_size) {
    ReplaceBuffer(frame, vertex_buffer_, total_vertex_size,
                  /* keep_content= */ true);
  }

  vk::DeviceSize index_size = kIndexStride * index_count;
  vk::DeviceSize total_index_size = index_buffer_->size() + index_size;
  if (index_buffer_->capacity() < total_index_size) {
    ReplaceBuffer(frame, index_buffer_, total_index_size,
                  /* keep_content= */ true);
  }

  return {vertex_buffer_->Reserve(vertex_size),
          index_buffer_->Reserve(index_size)};
};

void MeshBuffer::ProvideBuffersToScenicMesh(scenic::Mesh* scenic_mesh) {
  auto bb_min = bbox_.min();
  auto bb_max = bbox_.max();
  float bb_min_arr[] = {bb_min.x, bb_min.y, bb_min.z};
  float bb_max_arr[] = {bb_max.x, bb_max.y, bb_max.z};
  scenic_mesh->BindBuffers(
      index_buffer_->scenic_buffer(), kMeshIndexFormat, /* index_offset= */ 0,
      index_count_, vertex_buffer_->scenic_buffer(),
      scenic::NewMeshVertexFormat(kMeshVertexPositionType,
                                      kMeshVertexNormalType,
                                      kMeshVertexTexCoodType),
      /* vertex_offset= */ 0, vertex_count_, bb_min_arr, bb_max_arr);
}

void MeshBuffer::ReplaceBuffer(Frame* frame, SharedBufferPtr& shared_buffer,
                               vk::DeviceSize capacity_req, bool keep_content) {
  auto shared_buffer_pool = frame->shared_buffer_pool();
  if (!shared_buffer) {
    shared_buffer = shared_buffer_pool->GetBuffer(capacity_req);
    return;
  }

  auto new_buffer = shared_buffer_pool->GetBuffer(capacity_req);
  if (keep_content && shared_buffer->size() > 0) {
    new_buffer->Copy(frame, shared_buffer);
  }
  shared_buffer_pool->ReturnBuffer(std::move(shared_buffer),
                                   frame->DuplicateReleaseFence());
  shared_buffer = std::move(new_buffer);
}

}  // namespace sketchy_service
