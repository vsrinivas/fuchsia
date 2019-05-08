// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/impl/mesh_manager.h"

#include <iterator>

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/lib/escher/impl/command_buffer_pool.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/vk/buffer.h"
#include "src/ui/lib/escher/vk/gpu_allocator.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"

namespace escher {
namespace impl {

MeshManager::MeshManager(CommandBufferPool* command_buffer_pool,
                         GpuAllocator* allocator, GpuUploader* uploader,
                         ResourceRecycler* resource_recycler)
    : command_buffer_pool_(command_buffer_pool),
      allocator_(allocator),
      uploader_(uploader),
      resource_recycler_(resource_recycler),
      device_(command_buffer_pool->device()),
      queue_(command_buffer_pool->queue()),
      builder_count_(0) {}

MeshManager::~MeshManager() { FXL_DCHECK(builder_count_ == 0); }

MeshBuilderPtr MeshManager::NewMeshBuilder(const MeshSpec& spec,
                                           size_t max_vertex_count,
                                           size_t max_index_count) {
  FXL_DCHECK(spec.IsValidOneBufferMesh());
  size_t stride = spec.stride(0);
  return AdoptRef(new MeshManager::MeshBuilder(
      this, spec, max_vertex_count, max_index_count,
      uploader_->GetWriter(max_vertex_count * stride),
      uploader_->GetWriter(max_index_count * sizeof(uint32_t))));
}

MeshManager::MeshBuilder::MeshBuilder(MeshManager* manager,
                                      const MeshSpec& spec,
                                      size_t max_vertex_count,
                                      size_t max_index_count,
                                      GpuUploader::Writer vertex_writer,
                                      GpuUploader::Writer index_writer)
    : escher::MeshBuilder(max_vertex_count, max_index_count, spec.stride(0),
                          vertex_writer.ptr(),
                          reinterpret_cast<uint32_t*>(index_writer.ptr())),
      manager_(manager),
      spec_(spec),
      is_built_(false),
      vertex_writer_(std::move(vertex_writer)),
      index_writer_(std::move(index_writer)) {
  FXL_DCHECK(spec.IsValidOneBufferMesh());
}

MeshManager::MeshBuilder::~MeshBuilder() {}

BoundingBox MeshManager::MeshBuilder::ComputeBoundingBox2D() const {
  FXL_DCHECK(spec_.attribute_offset(0, MeshAttribute::kPosition2D) == 0);
  uint8_t* vertex_ptr = vertex_staging_buffer_;

  vec2* pos = reinterpret_cast<vec2*>(vertex_ptr);
  vec3 min(*pos, 0);
  vec3 max(*pos, 0);

  for (size_t i = 1; i < vertex_count_; ++i) {
    vertex_ptr += vertex_stride_;
    pos = reinterpret_cast<vec2*>(vertex_ptr);
    min = glm::min(min, vec3(*pos, 0));
    max = glm::max(max, vec3(*pos, 0));
  }

  return BoundingBox(min, max);
}

BoundingBox MeshManager::MeshBuilder::ComputeBoundingBox3D() const {
  FXL_DCHECK(spec_.attribute_offset(0, MeshAttribute::kPosition3D) == 0);
  uint8_t* vertex_ptr = vertex_staging_buffer_;

  vec3* pos = reinterpret_cast<vec3*>(vertex_ptr);
  vec3 min(*pos);
  vec3 max(*pos);

  for (size_t i = 1; i < vertex_count_; ++i) {
    vertex_ptr += vertex_stride_;
    pos = reinterpret_cast<vec3*>(vertex_ptr);
    min = glm::min(min, *pos);
    max = glm::max(max, *pos);
  }

  return BoundingBox(min, max);
}

BoundingBox MeshManager::MeshBuilder::ComputeBoundingBox() const {
  FXL_DCHECK(vertex_count_ > 0);
  FXL_DCHECK(spec_.IsValidOneBufferMesh());
  return spec_.has_attribute(0, MeshAttribute::kPosition2D)
             ? ComputeBoundingBox2D()
             : ComputeBoundingBox3D();
}

MeshPtr MeshManager::MeshBuilder::Build() {
  FXL_DCHECK(!is_built_);
  if (is_built_) {
    return MeshPtr();
  }
  is_built_ = true;

  vk::Device device = manager_->device_;
  GpuAllocator* allocator = manager_->allocator_;

  // TODO: use eTransferDstOptimal instead of eTransferDst?
  auto vertex_buffer = allocator->AllocateBuffer(
      manager_->resource_recycler(), vertex_count_ * vertex_stride_,
      vk::BufferUsageFlagBits::eVertexBuffer |
          vk::BufferUsageFlagBits::eTransferSrc |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);
  auto index_buffer = allocator->AllocateBuffer(
      manager_->resource_recycler(), index_count_ * sizeof(uint32_t),
      vk::BufferUsageFlagBits::eIndexBuffer |
          vk::BufferUsageFlagBits::eTransferSrc |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  vertex_writer_.WriteBuffer(vertex_buffer, {0, 0, vertex_buffer->size()},
                             Semaphore::New(device));
  vertex_writer_.Submit();

  index_writer_.WriteBuffer(index_buffer, {0, 0, index_buffer->size()},
                            SemaphorePtr());
  index_writer_.Submit();

  auto mesh = fxl::MakeRefCounted<Mesh>(
      manager_->resource_recycler(), spec_, ComputeBoundingBox(), vertex_count_,
      index_count_, vertex_buffer, std::move(index_buffer));

  mesh->SetWaitSemaphore(vertex_buffer->TakeWaitSemaphore());
  return mesh;
}

}  // namespace impl
}  // namespace escher
