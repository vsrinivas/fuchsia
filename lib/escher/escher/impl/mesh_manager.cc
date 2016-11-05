// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/mesh_manager.h"

#include <iterator>

#include "escher/geometry/types.h"
#include "escher/impl/mesh_impl.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/vk/vulkan_context.h"

namespace escher {
namespace impl {

MeshManager::MeshManager(const VulkanContext& context, GpuAllocator* allocator)
    : device_(context.device),
      queue_(context.queue),
      transfer_queue_(context.transfer_queue),
      allocator_(allocator),
      builder_count_(0),
      mesh_count_(0) {
  FTL_DCHECK(queue_);
  uint32_t queue_family_index = context.transfer_queue_family_index;
  if (!transfer_queue_) {
    transfer_queue_ = queue_;
    queue_family_index = context.queue_family_index;
  }

  vk::CommandPoolCreateInfo info;
  info.flags = vk::CommandPoolCreateFlagBits::eTransient;
  info.queueFamilyIndex = queue_family_index;
  command_pool_ = ESCHER_CHECKED_VK_RESULT(device_.createCommandPool(info));
}

MeshManager::~MeshManager() {
  FTL_DCHECK(builder_count_ == 0);
  FTL_DCHECK(mesh_count_ == 0);

  UpdateBusyResources();

  if (command_pool_) {
    device_.destroyCommandPool(command_pool_);
    command_pool_ = nullptr;
  }
}

void MeshManager::UpdateBusyResources() {
  while (!busy_resources_.empty()) {
    auto& busy = busy_resources_.front();
    if (vk::Result::eNotReady == device_.getFenceStatus(busy.fence)) {
      // The first item in the queue is not finished, so neither are the rest.
      break;
    }
    device_.destroyFence(busy.fence);
    device_.freeCommandBuffers(command_pool_, busy.command_buffer);
    free_staging_buffers_.push_back(std::move(busy.buffer1));
    free_staging_buffers_.push_back(std::move(busy.buffer2));
    busy_resources_.pop();
  }
}

Buffer MeshManager::GetStagingBuffer(uint32_t size) {
  UpdateBusyResources();

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

  vk::CommandBufferAllocateInfo allocate_info;
  allocate_info.commandPool = manager_->command_pool_;
  allocate_info.level = vk::CommandBufferLevel::ePrimary;
  allocate_info.commandBufferCount = 1;
  vk::CommandBuffer command_buffer =
      ESCHER_CHECKED_VK_RESULT(device.allocateCommandBuffers(allocate_info))[0];
  vk::Fence fence =
      ESCHER_CHECKED_VK_RESULT(device.createFence(vk::FenceCreateInfo()));
  SemaphorePtr semaphore = Semaphore::New(device);

  vk::CommandBufferBeginInfo begin_info;
  begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
  auto begin_result = command_buffer.begin(begin_info);
  FTL_CHECK(begin_result == vk::Result::eSuccess);

  vk::BufferCopy region;

  region.size = vertex_staging_buffer_.GetSize();
  command_buffer.copyBuffer(vertex_staging_buffer_.buffer(),
                            vertex_buffer.buffer(), 1, &region);

  region.size = index_staging_buffer_.GetSize();
  command_buffer.copyBuffer(index_staging_buffer_.buffer(),
                            index_buffer.buffer(), 1, &region);

  auto end_result = command_buffer.end();
  FTL_CHECK(end_result == vk::Result::eSuccess);

  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;
  submit_info.signalSemaphoreCount = 1;
  auto s = semaphore->value();
  submit_info.pSignalSemaphores = &s;
  auto submit_result = manager_->transfer_queue_.submit(1, &submit_info, fence);
  FTL_CHECK(submit_result == vk::Result::eSuccess);

  manager_->busy_resources_.push({fence, std::move(vertex_staging_buffer_),
                                  std::move(index_staging_buffer_),
                                  command_buffer});

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
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    impl->attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttributeFlagBits::kPositionOffset) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = 1;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    impl->attributes.push_back(attribute);
  }
  if (spec.flags & MeshAttributeFlagBits::kUV) {
    vk::VertexInputAttributeDescription attribute;
    attribute.location = 2;
    attribute.binding = 0;
    attribute.format = vk::Format::eR32G32Sfloat;
    attribute.offset = stride;

    stride += sizeof(vec2);
    impl->attributes.push_back(attribute);
  }

  impl->binding.binding = 0;
  impl->binding.stride = stride;
  impl->binding.inputRate = vk::VertexInputRate::eVertex;

  // TODO: We currenty hardcode support for a single mesh layout.
  FTL_CHECK(spec.flags ==
            (MeshAttributeFlags(MeshAttributeFlagBits::kPosition) |
             MeshAttributeFlagBits::kUV));
  FTL_CHECK(stride == 4 * sizeof(float));

  ptr = impl.get();
  spec_cache_[spec] = std::move(impl);
  return *ptr;
}

}  // namespace impl
}  // namespace escher
