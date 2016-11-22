// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/mesh_manager.h"

#include <iterator>

#include "escher/geometry/types.h"
#include "escher/impl/command_buffer_pool.h"
#include "escher/impl/mesh_impl.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/vk/buffer.h"
#include "escher/vk/vulkan_context.h"

namespace escher {
namespace impl {

MeshManager::MeshManager(CommandBufferPool* command_buffer_pool,
                         GpuAllocator* allocator,
                         GpuUploader* uploader)
    : command_buffer_pool_(command_buffer_pool),
      allocator_(allocator),
      uploader_(uploader),
      device_(command_buffer_pool->device()),
      queue_(command_buffer_pool->queue()),
      builder_count_(0),
      mesh_count_(0) {}

MeshManager::~MeshManager() {
  FTL_DCHECK(builder_count_ == 0);
  FTL_DCHECK(mesh_count_ == 0);
}

MeshBuilderPtr MeshManager::NewMeshBuilder(const MeshSpec& spec,
                                           size_t max_vertex_count,
                                           size_t max_index_count) {
  auto& spec_impl = GetMeshSpecImpl(spec);
  return AdoptRef(new MeshManager::MeshBuilder(
      this, spec, max_vertex_count, max_index_count,
      uploader_->GetWriter(max_vertex_count * spec_impl.binding.stride),
      uploader_->GetWriter(max_index_count * sizeof(uint32_t)), spec_impl));
}

MeshManager::MeshBuilder::MeshBuilder(MeshManager* manager,
                                      const MeshSpec& spec,
                                      size_t max_vertex_count,
                                      size_t max_index_count,
                                      GpuUploader::Writer vertex_writer,
                                      GpuUploader::Writer index_writer,
                                      const MeshSpecImpl& spec_impl)
    : escher::MeshBuilder(max_vertex_count,
                          max_index_count,
                          spec_impl.binding.stride,
                          vertex_writer.ptr(),
                          reinterpret_cast<uint32_t*>(index_writer.ptr())),
      manager_(manager),
      spec_(spec),
      is_built_(false),
      vertex_writer_(std::move(vertex_writer)),
      index_writer_(std::move(index_writer)),
      spec_impl_(spec_impl) {}

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
  auto vertex_buffer = ftl::MakeRefCounted<Buffer>(
      device, allocator, vertex_count_ * vertex_stride_,
      vk::BufferUsageFlagBits::eVertexBuffer |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);
  auto index_buffer = ftl::MakeRefCounted<Buffer>(
      device, allocator, index_count_ * sizeof(uint32_t),
      vk::BufferUsageFlagBits::eIndexBuffer |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal);

  vertex_writer_.WriteBuffer(vertex_buffer, {0, 0, vertex_buffer->size()},
                             Semaphore::New(device));
  vertex_writer_.Submit();

  index_writer_.WriteBuffer(index_buffer, {0, 0, index_buffer->size()},
                            SemaphorePtr());
  index_writer_.Submit();

  auto mesh = ftl::MakeRefCounted<MeshImpl>(
      spec_, vertex_count_, index_count_, manager_, vertex_buffer,
      std::move(index_buffer), spec_impl_);

  mesh->SetWaitSemaphore(vertex_buffer->TakeWaitSemaphore());
  return mesh;
}

size_t MeshManager::MeshBuilder::GetAttributeOffset(MeshAttribute flag) {
  // Find the attribute location corresponding to the flag.
  uint32_t location = static_cast<uint32_t>(-1);
  switch (flag) {
    case MeshAttribute::kPosition:
      location = MeshImpl::kPositionAttributeLocation;
      break;
    case MeshAttribute::kPositionOffset:
      location = MeshImpl::kPositionOffsetAttributeLocation;
      break;
    case MeshAttribute::kUV:
      location = MeshImpl::kUVAttributeLocation;
      break;
    case MeshAttribute::kPerimeterPos:
      location = MeshImpl::kPerimeterPosAttributeLocation;
      break;
  }

  // Return offset of the attribute whose location matches.
  for (auto& attr : spec_impl_.attributes) {
    if (attr.location == location) {
      return attr.offset;
    }
  }
  FTL_CHECK(0);
  return 0;
}

const MeshSpecImpl& MeshManager::GetMeshSpecImpl(MeshSpec spec) {
  auto ptr = spec_cache_[spec].get();
  if (ptr) {
    return *ptr;
  }

  auto impl = std::make_unique<MeshSpecImpl>();

  vk::DeviceSize stride = 0;
  if (spec.flags & MeshAttribute::kPosition) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = MeshImpl::kPositionAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    impl->attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttribute::kPositionOffset) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = MeshImpl::kPositionOffsetAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    impl->attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttribute::kUV) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = MeshImpl::kUVAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    impl->attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttribute::kPerimeterPos) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = MeshImpl::kPerimeterPosAttributeLocation;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32Sfloat;
    attribute.offset = stride;

    stride += sizeof(float);
    impl->attributes.push_back(attribute);
  }

  impl->binding.binding = 0;
  impl->binding.stride = stride;
  impl->binding.inputRate = vk::VertexInputRate::eVertex;

  ptr = impl.get();
  spec_cache_[spec] = std::move(impl);
  return *ptr;
}

}  // namespace impl
}  // namespace escher
