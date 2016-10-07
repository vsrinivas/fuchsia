// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <list>
#include <queue>
#include <vulkan/vulkan.hpp>

#include "escher/impl/buffer.h"
#include "escher/shape/mesh_builder.h"
#include "escher/vk/vulkan_context.h"
#include "ftl/memory/ref_ptr.h"

namespace escher {
namespace impl {
class GpuAllocator;

// Responsible for generating Meshes, tracking their memory use, managing
// synchronization, etc.
class MeshManager {
 public:
  MeshManager(const VulkanContext& context, GpuAllocator* allocator);
  ~MeshManager();

  MeshBuilderPtr NewMeshBuilder(const MeshSpec& spec,
                                size_t max_vertex_count,
                                size_t max_index_count);

  void Update(uint64_t last_finished_frame);

  void DestroyMeshResources(uint64_t last_rendered_frame,
                            Buffer vertex_buffer,
                            Buffer index_buffer,
                            vk::Semaphore mesh_ready_semaphore_);

 private:
  Buffer GetStagingBuffer(uint32_t size);
  void UpdateBusyResources();

  class MeshBuilder : public escher::MeshBuilder {
   public:
    MeshBuilder(MeshManager* manager,
                const MeshSpec& spec,
                size_t max_vertex_count,
                size_t max_index_count,
                Buffer vertex_staging_buffer,
                Buffer index_staging_buffer);
    ~MeshBuilder() override;

    MeshPtr Build() override;

   private:
    MeshManager* manager_;
    MeshSpec spec_;
    bool is_built_;
    Buffer vertex_staging_buffer_;
    Buffer index_staging_buffer_;
  };

  vk::Device device_;
  vk::Queue queue_;
  vk::Queue transfer_queue_;
  GpuAllocator* allocator_;

  vk::CommandPool command_pool_;

  struct BusyResources {
    vk::Fence fence;
    Buffer buffer1;
    Buffer buffer2;
    vk::CommandBuffer command_buffer;
  };
  std::list<Buffer> free_staging_buffers_;
  std::queue<BusyResources> busy_resources_;

  struct DoomedResources {
    std::vector<Buffer> buffers;
    std::vector<vk::Semaphore> semaphores;
  };
  // Map of resources that are no longer used and should be destroyed.  The keys
  // are the last frame that the resources were used.
  std::map<uint64_t, DoomedResources> doomed_resources_;

  std::atomic<uint32_t> builder_count_;
  std::atomic<uint32_t> mesh_count_;
};

}  // namespace impl
}  // namespace escher
