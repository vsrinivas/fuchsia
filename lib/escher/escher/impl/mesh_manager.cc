// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/mesh_manager.h"

#include <iterator>

#include "escher/geometry/types.h"
#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/mesh_impl.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/vk/vulkan_context.h"

namespace escher {
namespace impl {

MeshManager::MeshManager(CommandBufferPool* command_buffer_pool,
                         GpuAllocator* allocator)
    : command_buffer_pool_(command_buffer_pool),
      allocator_(allocator),
      device_(command_buffer_pool->device()),
      queue_(command_buffer_pool->queue()),
      builder_count_(0),
      mesh_count_(0) {}

MeshManager::~MeshManager() {
  FTL_DCHECK(builder_count_ == 0);
  FTL_DCHECK(mesh_count_ == 0);
}

Buffer MeshManager::GetStagingBuffer(uint32_t size) {
  auto it = free_staging_buffers_.begin();
  while (it != free_staging_buffers_.end()) {
    if (it->GetSize() > size) {
      // Found a large-enough buffer.
      // TODO: cleanup
      auto mit = std::make_move_iterator(it);
      Buffer buf(std::move(*mit));
      free_staging_buffers_.erase(it);
      return buf;
    }
    ++it;
  }
  // Couldn't find a large enough buffer, so create a new one.
  return Buffer(device_, allocator_, size,
                vk::BufferUsageFlagBits::eTransferSrc,
                vk::MemoryPropertyFlagBits::eHostVisible);
}

MeshBuilderPtr MeshManager::NewMeshBuilder(const MeshSpec& spec,
                                           size_t max_vertex_count,
                                           size_t max_index_count) {
  auto& spec_impl = GetMeshSpecImpl(spec);
  return AdoptRef(new MeshManager::MeshBuilder(
      this, spec, max_vertex_count, max_index_count,
      GetStagingBuffer(max_vertex_count * spec_impl.binding.stride),
      GetStagingBuffer(max_index_count * sizeof(uint32_t)), spec_impl));
}

MeshManager::MeshBuilder::MeshBuilder(MeshManager* manager,
                                      const MeshSpec& spec,
                                      size_t max_vertex_count,
                                      size_t max_index_count,
                                      Buffer vertex_staging_buffer,
                                      Buffer index_staging_buffer,
                                      const MeshSpecImpl& spec_impl)
    : escher::MeshBuilder(
          max_vertex_count,
          max_index_count,
          spec_impl.binding.stride,
          reinterpret_cast<uint8_t*>(vertex_staging_buffer.Map()),
          reinterpret_cast<uint32_t*>(index_staging_buffer.Map())),
      manager_(manager),
      spec_(spec),
      is_built_(false),
      vertex_staging_buffer_(std::move(vertex_staging_buffer)),
      index_staging_buffer_(std::move(index_staging_buffer)),
      spec_impl_(spec_impl) {}

MeshManager::MeshBuilder::~MeshBuilder() {
  if (!is_built_) {
    // The MeshBuilder was destroyed before Build() was called.
    manager_->free_staging_buffers_.push_back(
        std::move(vertex_staging_buffer_));
    manager_->free_staging_buffers_.push_back(std::move(index_staging_buffer_));
  }
}

MeshPtr MeshManager::MeshBuilder::Build() {
  FTL_DCHECK(!is_built_);
  if (is_built_) {
    return MeshPtr();
  }
  is_built_ = true;

  vertex_staging_buffer_.Unmap();
  index_staging_buffer_.Unmap();

  vk::Device device = manager_->device_;
  GpuAllocator* allocator = manager_->allocator_;

  // TODO: use eTransferDstOptimal instead of eTransferDst?
  Buffer vertex_buffer(device, allocator, vertex_staging_buffer_.GetSize(),
                       vk::BufferUsageFlagBits::eVertexBuffer |
                           vk::BufferUsageFlagBits::eTransferDst,
                       vk::MemoryPropertyFlagBits::eDeviceLocal);
  Buffer index_buffer(device, allocator, index_staging_buffer_.GetSize(),
                      vk::BufferUsageFlagBits::eIndexBuffer |
                          vk::BufferUsageFlagBits::eTransferDst,
                      vk::MemoryPropertyFlagBits::eDeviceLocal);

  auto command_buffer = manager_->command_buffer_pool_->GetCommandBuffer();

  SemaphorePtr semaphore = Semaphore::New(device);
  command_buffer->AddSignalSemaphore(semaphore);

  vk::BufferCopy region;

  region.size = vertex_staging_buffer_.GetSize();
  command_buffer->get().copyBuffer(vertex_staging_buffer_.buffer(),
                                   vertex_buffer.buffer(), 1, &region);

  region.size = index_staging_buffer_.GetSize();
  command_buffer->get().copyBuffer(index_staging_buffer_.buffer(),
                                   index_buffer.buffer(), 1, &region);

  // Keep this builder alive until submission has finished.
  ftl::RefPtr<MeshBuilder> me(this);
  command_buffer->Submit(manager_->queue_, [me{std::move(me)}]() {
    auto& bufs = me->manager_->free_staging_buffers_;
    bufs.push_back(std::move(me->vertex_staging_buffer_));
    bufs.push_back(std::move(me->index_staging_buffer_));
  });

  auto mesh = ftl::MakeRefCounted<MeshImpl>(
      spec_, vertex_count_, index_count_, manager_, std::move(vertex_buffer),
      std::move(index_buffer), spec_impl_);
  mesh->SetWaitSemaphore(std::move(semaphore));
  return mesh;
}

const MeshSpecImpl& MeshManager::GetMeshSpecImpl(MeshSpec spec) {
  auto ptr = spec_cache_[spec].get();
  if (ptr) {
    return *ptr;
  }

  auto impl = std::make_unique<MeshSpecImpl>();

  vk::DeviceSize stride = 0;
  if (spec.flags & MeshAttributeFlagBits::kPosition) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = MeshImpl::kPositionAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    impl->attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttributeFlagBits::kPositionOffset) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = MeshImpl::kPositionOffsetAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    impl->attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttributeFlagBits::kUV) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = MeshImpl::kUVAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    impl->attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttributeFlagBits::kPerimeter) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = MeshImpl::kPerimeterAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32Sfloat;
    attribute.offset = stride;

    stride += sizeof(float);
    impl->attributes.push_back(attribute);
  }

  impl->binding.binding = 0;
  impl->binding.stride = stride;
  impl->binding.inputRate = vk::VertexInputRate::eVertex;

  // TODO: We currenty hardcode support for a single mesh layout.
  FTL_CHECK(spec.flags ==
            (MeshAttributeFlagBits::kPosition | MeshAttributeFlagBits::kUV));
  FTL_CHECK(stride == 4 * sizeof(float));

  ptr = impl.get();
  spec_cache_[spec] = std::move(impl);
  return *ptr;
}

}  // namespace impl
}  // namespace escher
