// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_MESH_MANAGER_H_
#define LIB_ESCHER_IMPL_MESH_MANAGER_H_

#include <atomic>
#include <list>
#include <queue>
#include <unordered_map>

#include <vulkan/vulkan.hpp>

#include "lib/escher/impl/gpu_uploader.h"
#include "lib/escher/shape/mesh_builder.h"
#include "lib/escher/shape/mesh_builder_factory.h"
#include "lib/escher/vk/vulkan_context.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace escher {
namespace impl {
class GpuUploader;

// Responsible for generating Meshes, tracking their memory use, managing
// synchronization, etc.
//
// Not thread-safe.
class MeshManager : public MeshBuilderFactory {
 public:
  MeshManager(CommandBufferPool* command_buffer_pool, GpuAllocator* allocator,
              GpuUploader* uploader, ResourceRecycler* resource_recycler);
  virtual ~MeshManager();

  // The returned MeshBuilder is not thread-safe.
  MeshBuilderPtr NewMeshBuilder(const MeshSpec& spec, size_t max_vertex_count,
                                size_t max_index_count) override;

  ResourceRecycler* resource_recycler() const { return resource_recycler_; }

 private:
  void UpdateBusyResources();

  class MeshBuilder : public escher::MeshBuilder {
   public:
    MeshBuilder(MeshManager* manager, const MeshSpec& spec,
                size_t max_vertex_count, size_t max_index_count,
                GpuUploader::Writer vertex_writer,
                GpuUploader::Writer index_writer);
    ~MeshBuilder() override;

    MeshPtr Build() override;

   private:
    BoundingBox ComputeBoundingBox() const;
    BoundingBox ComputeBoundingBox2D() const;
    BoundingBox ComputeBoundingBox3D() const;

    MeshManager* manager_;
    MeshSpec spec_;
    bool is_built_;
    GpuUploader::Writer vertex_writer_;
    GpuUploader::Writer index_writer_;
  };

  CommandBufferPool* const command_buffer_pool_;
  GpuAllocator* const allocator_;
  GpuUploader* const uploader_;
  ResourceRecycler* const resource_recycler_;
  const vk::Device device_;
  const vk::Queue queue_;

  std::atomic<uint32_t> builder_count_;
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_MESH_MANAGER_H_
