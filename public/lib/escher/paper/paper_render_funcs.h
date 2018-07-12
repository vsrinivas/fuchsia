// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_PAPER_PAPER_RENDER_FUNCS_H_
#define LIB_ESCHER_PAPER_PAPER_RENDER_FUNCS_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/forward_declarations.h"

namespace escher {

// Helper functions and structs used by e.g. PaperRenderQueue to produce
// RenderQueueItems for the RenderQueues that it encapsulates.
class PaperRenderFuncs {
 public:
  // Matches signature of |RenderQueueItem::RenderFunc|.  Expects the items'
  // |object_data| and |instance_data| to be of type |MeshObjectData| and
  // |MeshInstanceData|, respectively; these types are defined below.
  static void RenderMesh(CommandBuffer* cb, const RenderQueueItem* items,
                         uint32_t instance_count);

  // Struct referenced by |MeshObjectData|, see below.
  struct VertexBinding {
    uint32_t binding_index;
    Buffer* buffer;
    uint64_t offset;
    uint64_t stride;
  };

  // Struct referenced by |MeshObjectData| and |MeshInstanceData|, see below.
  // Typically populated from a |UniformAllocation|, such as obtained from a
  // |UniformBlockAllocator|.
  struct UniformBinding {
    uint32_t descriptor_set_index;
    uint32_t binding_index;
    Buffer* buffer;
    vk::DeviceSize offset;
    vk::DeviceSize size;
  };

  // Struct referenced by |MeshObjectData|, see below.
  struct VertexAttributeBinding {
    uint32_t binding_index;
    uint32_t attribute_index;
    vk::Format format;
    uint64_t offset;
  };

  // Struct intended to be used as the |object_data| of a |RenderQueueItem|.
  // Typically populated via the helper function NewMeshObjectData(); see below.
  struct MeshObjectData {
    vk::Buffer index_buffer;
    vk::IndexType index_type;
    uint64_t index_buffer_offset;
    uint32_t num_indices;

    uint32_t vertex_binding_count;
    VertexBinding* vertex_bindings;

    uint32_t vertex_attribute_count;
    VertexAttributeBinding* vertex_attributes;

    uint32_t uniform_binding_count;
    UniformBinding* uniform_bindings;

    Texture* texture;
    ShaderProgram* shader_program;
  };

  // Struct intended to be used as the |instance_data| of a |RenderQueueItem|.
  struct MeshInstanceData {
    UniformBinding object_properties;
  };

  // Helper function for allocating/populating a |MeshObjectData|.  Both CPU and
  // uniform GPU memory is allocated using per-Frame allocators.
  static PaperRenderFuncs::MeshObjectData* NewMeshObjectData(
      const FramePtr& frame, const MeshPtr& mesh, const TexturePtr& texture,
      const ShaderProgramPtr& program,
      const UniformAllocation& view_projection_uniform);
};

}  // namespace escher

#endif  // LIB_ESCHER_PAPER_PAPER_RENDER_FUNCS_H_
