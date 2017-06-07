// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/mesh_manager.h"

#include <iterator>

#include "escher/geometry/types.h"
#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/resources/resource_life_preserver.h"
#include "escher/vk/buffer.h"
#include "escher/vk/vulkan_context.h"

namespace escher {
namespace impl {

MeshManager::MeshManager(CommandBufferPool* command_buffer_pool,
                         GpuAllocator* allocator,
                         GpuUploader* uploader,
                         ResourceLifePreserver* life_preserver)
    : command_buffer_pool_(command_buffer_pool),
      allocator_(allocator),
      uploader_(uploader),
      life_preserver_(life_preserver),
      device_(command_buffer_pool->device()),
      queue_(command_buffer_pool->queue()),
      builder_count_(0) {}

MeshManager::~MeshManager() {
  FTL_DCHECK(builder_count_ == 0);
}

MeshBuilderPtr MeshManager::NewMeshBuilder(const MeshSpec& spec,
                                           size_t max_vertex_count,
                                           size_t max_index_count) {
  size_t stride = spec.GetStride();
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
    : escher::MeshBuilder(max_vertex_count,
                          max_index_count,
                          spec.GetStride(),
                          vertex_writer.ptr(),
                          reinterpret_cast<uint32_t*>(index_writer.ptr())),
      manager_(manager),
      spec_(spec),
      is_built_(false),
      vertex_writer_(std::move(vertex_writer)),
      index_writer_(std::move(index_writer)) {}

MeshManager::MeshBuilder::~MeshBuilder() {}

MeshPtr MeshManager::MeshBuilder::Build() {
  FTL_DCHECK(!is_built_);
  if (is_built_) {
    return MeshPtr();
  }
  is_built_ = true;

  vk::Device device = manager_->device_;
  GpuAllocator* allocator = manager_->allocator_;

  // TODO: use eTransferDstOptimal instead of eTransferDst?
  auto vertex_buffer = Buffer::New(manager_->life_preserver(), allocator,
                                   vertex_count_ * vertex_stride_,
                                   vk::BufferUsageFlagBits::eVertexBuffer |
                                       vk::BufferUsageFlagBits::eTransferDst,
                                   vk::MemoryPropertyFlagBits::eDeviceLocal);
  auto index_buffer = Buffer::New(manager_->life_preserver(), allocator,
                                  index_count_ * sizeof(uint32_t),
                                  vk::BufferUsageFlagBits::eIndexBuffer |
                                      vk::BufferUsageFlagBits::eTransferDst,
                                  vk::MemoryPropertyFlagBits::eDeviceLocal);

  vertex_writer_.WriteBuffer(vertex_buffer, {0, 0, vertex_buffer->size()},
                             Semaphore::New(device));
  vertex_writer_.Submit();

  index_writer_.WriteBuffer(index_buffer, {0, 0, index_buffer->size()},
                            SemaphorePtr());
  index_writer_.Submit();

  auto mesh = ftl::MakeRefCounted<Mesh>(manager_->life_preserver(), spec_,
                                        vertex_count_, index_count_,
                                        vertex_buffer, std::move(index_buffer));

  mesh->SetWaitSemaphore(vertex_buffer->TakeWaitSemaphore());
  return mesh;
}

}  // namespace impl
}  // namespace escher
