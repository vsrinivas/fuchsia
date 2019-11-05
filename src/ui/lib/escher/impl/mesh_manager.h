// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_IMPL_MESH_MANAGER_H_
#define SRC_UI_LIB_ESCHER_IMPL_MESH_MANAGER_H_

#include <atomic>
#include <list>
#include <queue>
#include <unordered_map>

#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/shape/mesh_builder.h"
#include "src/ui/lib/escher/shape/mesh_builder_factory.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace impl {

// Responsible for generating Meshes, tracking their memory use, managing
// synchronization, etc.
//
// Not thread-safe.
class MeshManager : public MeshBuilderFactory {
 public:
  MeshManager(CommandBufferPool* command_buffer_pool, GpuAllocator* allocator,
              ResourceRecycler* resource_recycler);
  virtual ~MeshManager();

  // The returned MeshBuilder is not thread-safe.
  MeshBuilderPtr NewMeshBuilder(BatchGpuUploader* gpu_uploader, const MeshSpec& spec,
                                size_t max_vertex_count, size_t max_index_count) override;

  ResourceRecycler* resource_recycler() const { return resource_recycler_; }

 private:
  void UpdateBusyResources();

  class MeshBuilder : public escher::MeshBuilder {
   public:
    MeshBuilder(MeshManager* manager, const MeshSpec& spec, size_t max_vertex_count,
                size_t max_index_count, uint8_t* vertex_ptr, uint32_t* index_ptr,
                std::unique_ptr<BatchGpuUploader::Writer> writer, BatchGpuUploader* uploader);
    ~MeshBuilder() override;

    MeshPtr Build() override;

   private:
    BoundingBox ComputeBoundingBox() const;
    BoundingBox ComputeBoundingBox2D() const;
    BoundingBox ComputeBoundingBox3D() const;

    MeshManager* manager_;
    MeshSpec spec_;
    bool is_built_;

    std::unique_ptr<BatchGpuUploader::Writer> writer_;

    // We retain a raw ptr to the uploader so that we can call PostWriter() on it.  There
    // is a DCHECK in the BatchGpuUploader destructor that ensures that all readers/writers
    // have been posted.
    // TODO(40170): make BatchGpuUploader::Writer responsible for reffing the uploader.
    BatchGpuUploader* gpu_uploader_;
  };

  CommandBufferPool* const command_buffer_pool_;
  GpuAllocator* const allocator_;
  ResourceRecycler* const resource_recycler_;
  const vk::Device device_;
  const vk::Queue queue_;

  std::atomic<uint32_t> builder_count_;
};

}  // namespace impl
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_IMPL_MESH_MANAGER_H_
