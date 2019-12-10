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

MeshManager::MeshManager(CommandBufferPool* command_buffer_pool, GpuAllocator* allocator,
                         ResourceRecycler* resource_recycler)
    : command_buffer_pool_(command_buffer_pool),
      allocator_(allocator),
      resource_recycler_(resource_recycler),
      device_(command_buffer_pool->device()),
      queue_(command_buffer_pool->queue()),
      builder_count_(0) {}

MeshManager::~MeshManager() { FXL_DCHECK(builder_count_ == 0); }

MeshBuilderPtr MeshManager::NewMeshBuilder(BatchGpuUploader* gpu_uploader, const MeshSpec& spec,
                                           size_t max_vertex_count, size_t max_index_count) {
  FXL_DCHECK(spec.IsValidOneBufferMesh());
  size_t stride = spec.stride(0);

  return AdoptRef(new MeshManager::MeshBuilder(this, spec, max_vertex_count, max_index_count,
                                               std::move(gpu_uploader)));
}

MeshManager::MeshBuilder::MeshBuilder(MeshManager* manager, const MeshSpec& spec,
                                      size_t max_vertex_count, size_t max_index_count,
                                      BatchGpuUploader* gpu_uploader)
    : escher::MeshBuilder(max_vertex_count, max_index_count, spec.stride(0)),
      manager_(manager),
      spec_(spec),
      is_built_(false),
      gpu_uploader_(gpu_uploader) {
  FXL_DCHECK(spec.IsValidOneBufferMesh());
}

MeshManager::MeshBuilder::~MeshBuilder() {}

BoundingBox MeshManager::MeshBuilder::ComputeBoundingBox2D() const {
  FXL_DCHECK(spec_.attribute_offset(0, MeshAttribute::kPosition2D) == 0);
  const uint8_t* vertex_ptr = vertex_staging_buffer_.data();

  const vec2* pos = reinterpret_cast<const vec2*>(vertex_ptr);
  vec3 min(*pos, 0);
  vec3 max(*pos, 0);

  for (size_t i = 1; i < vertex_count_; ++i) {
    vertex_ptr += vertex_stride_;
    pos = reinterpret_cast<const vec2*>(vertex_ptr);
    min = glm::min(min, vec3(*pos, 0));
    max = glm::max(max, vec3(*pos, 0));
  }

  return BoundingBox(min, max);
}

BoundingBox MeshManager::MeshBuilder::ComputeBoundingBox3D() const {
  FXL_DCHECK(spec_.attribute_offset(0, MeshAttribute::kPosition3D) == 0);
  const uint8_t* vertex_ptr = vertex_staging_buffer_.data();

  const vec3* pos = reinterpret_cast<const vec3*>(vertex_ptr);
  vec3 min(*pos);
  vec3 max(*pos);

  for (size_t i = 1; i < vertex_count_; ++i) {
    vertex_ptr += vertex_stride_;
    pos = reinterpret_cast<const vec3*>(vertex_ptr);
    min = glm::min(min, *pos);
    max = glm::max(max, *pos);
  }

  return BoundingBox(min, max);
}

BoundingBox MeshManager::MeshBuilder::ComputeBoundingBox() const {
  FXL_DCHECK(vertex_count_ > 0);
  FXL_DCHECK(spec_.IsValidOneBufferMesh());
  return spec_.has_attribute(0, MeshAttribute::kPosition2D) ? ComputeBoundingBox2D()
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
      vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferSrc |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);
  auto index_buffer = allocator->AllocateBuffer(
      manager_->resource_recycler(), index_count_ * sizeof(uint32_t),
      vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferSrc |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  // Calculate bounding box using the staging buffers before they are cleared.
  auto bounding_box = ComputeBoundingBox();

  // |vertex_staging_buffer_| and |index_staging_buffer_| will be cleared after
  // being moved to BatchGpuUploader, so that we will be able to reuse
  // MeshBuilder.
  gpu_uploader_->ScheduleWriteBuffer(vertex_buffer, std::move(vertex_staging_buffer_),
                                     /* target_offset */ 0,
                                     /* copy_size */ vertex_count_ * vertex_stride_);
  gpu_uploader_->ScheduleWriteBuffer(index_buffer, std::move(index_staging_buffer_),
                                     /* target_offset */ 0,
                                     /* copy_size */ index_count_ * sizeof(uint32_t));

  auto result = fxl::MakeRefCounted<Mesh>(manager_->resource_recycler(), spec_,
                                          std::move(bounding_box), vertex_count_, index_count_,
                                          std::move(vertex_buffer), std::move(index_buffer));

  // Clear the vertex staging buffer and index staging buffer for future reuse.
  vertex_count_ = 0U;
  index_count_ = 0;

  return result;
}

}  // namespace impl
}  // namespace escher
