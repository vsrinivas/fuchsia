// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <list>
#include <queue>
#include <unordered_map>

#include <vulkan/vulkan.hpp>

#include "escher/impl/buffer.h"
#include "escher/impl/mesh_impl.h"
#include "escher/shape/mesh_builder.h"
#include "escher/shape/mesh_builder_factory.h"
#include "escher/vk/vulkan_context.h"
#include "ftl/memory/ref_ptr.h"

namespace escher {
namespace impl {
class GpuAllocator;

// Responsible for generating Meshes, tracking their memory use, managing
// synchronization, etc.
//
// Not thread-safe.
class MeshManager : public MeshBuilderFactory {
 public:
  MeshManager(CommandBufferPool* command_buffer_pool, GpuAllocator* allocator);
  ~MeshManager();

  // The returned MeshBuilder is not thread-safe.
  MeshBuilderPtr NewMeshBuilder(const MeshSpec& spec,
                                size_t max_vertex_count,
                                size_t max_index_count) override;

  const MeshSpecImpl& GetMeshSpecImpl(MeshSpec spec);

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
                Buffer index_staging_buffer,
                const MeshSpecImpl& spec_impl);
    ~MeshBuilder() override;

    MeshPtr Build() override;

    // Return the byte-offset of the attribute within each vertex.
    size_t GetAttributeOffset(MeshAttributeFlagBits flag) override;

   private:
    MeshManager* manager_;
    MeshSpec spec_;
    bool is_built_;
    Buffer vertex_staging_buffer_;
    Buffer index_staging_buffer_;
    const MeshSpecImpl& spec_impl_;
  };

  friend class MeshImpl;
  void IncrementMeshCount() { ++mesh_count_; }
  void DecrementMeshCount() { --mesh_count_; }

  CommandBufferPool* command_buffer_pool_;
  GpuAllocator* allocator_;
  vk::Device device_;
  vk::Queue queue_;
  std::list<Buffer> free_staging_buffers_;

  std::unordered_map<MeshSpec, std::unique_ptr<MeshSpecImpl>, MeshSpec::Hash>
      spec_cache_;

  std::atomic<uint32_t> builder_count_;
  std::atomic<uint32_t> mesh_count_;
};

}  // namespace impl
}  // namespace escher
