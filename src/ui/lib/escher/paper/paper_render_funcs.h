// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_FUNCS_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_FUNCS_H_

#include <vulkan/vulkan.hpp>

#include "src/ui/lib/escher/paper/paper_readme.h"

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/paper/paper_drawable_flags.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/renderer/uniform_binding.h"
#include "src/ui/lib/escher/vk/command_buffer.h"

namespace escher {

struct RenderQueueContext;
struct RenderQueueItem;

// Helper functions and structs used by e.g. PaperRenderQueue to produce
// RenderQueueItems for the RenderQueues that it encapsulates.
class PaperRenderFuncs {
 public:
  // Matches signature of |RenderQueueItem::RenderFunc|.  Expects the items'
  // |object_data| and |instance_data| to be of type |MeshData| and
  // |MeshDrawData|, respectively; these types are defined below.
  static void RenderMesh(CommandBuffer* cb, const RenderQueueContext* context,
                         const RenderQueueItem* items, uint32_t instance_count);

  // Struct referenced by |MeshData|, see below.
  struct IndexBinding {
    vk::Buffer index_buffer;
    vk::IndexType index_type;
    uint64_t index_buffer_offset;

    void Bind(CommandBuffer* cb) const {
      cb->BindIndices(index_buffer, index_buffer_offset, index_type);
    }
  };

  // Struct referenced by |MeshData|, see below.
  struct VertexBinding {
    uint32_t binding_index;
    vk::Buffer buffer;
    uint64_t offset;
    uint32_t stride;

    void Bind(CommandBuffer* cb) const {
      cb->BindVertices(binding_index, buffer, offset, stride);
    }
  };

  // Struct referenced by |MeshData|, see below.
  struct VertexAttributeBinding {
    uint32_t binding_index;
    uint32_t attribute_index;
    vk::Format format;
    uint32_t offset;

    void Bind(CommandBuffer* cb) const {
      cb->SetVertexAttributes(binding_index, attribute_index, format, offset);
    }
  };

  // Struct intended to be used as the |object_data| of a |RenderQueueItem|.
  // Typically populated via the helper function NewMeshData(); see below.
  struct MeshData {
    IndexBinding index_binding;
    uint32_t num_indices;
    uint32_t num_shadow_volume_indices;

    uint32_t vertex_binding_count;
    VertexBinding* vertex_bindings;

    uint32_t vertex_attribute_count;
    VertexAttributeBinding* vertex_attributes;

    uint32_t uniform_binding_count;
    UniformBinding* uniform_bindings;

    // TODO(ES-159): Texture bindings, not just one texture hardcoded to (1,1).
    Texture* texture;

    void Bind(CommandBuffer* cb) const;
  };

  // Struct intended to be used as the |instance_data| of a |RenderQueueItem|.
  struct MeshDrawData {
    UniformBinding object_properties;
    PaperDrawableFlags flags;
  };

  // Helper function for allocating/populating a |MeshData|.  Both CPU and
  // uniform GPU memory is allocated using per-Frame allocators.
  static PaperRenderFuncs::MeshData* NewMeshData(
      const FramePtr& frame, Mesh* mesh, const TexturePtr& texture,
      uint32_t num_indices, uint32_t num_shadow_volume_indices);

  // Helper function for allocating/populating a |MeshDrawData|.  Both CPU and
  // uniform GPU memory is allocated using per-Frame allocators.
  static PaperRenderFuncs::MeshDrawData* NewMeshDrawData(
      const FramePtr& frame, const mat4& transform, const vec4& color,
      PaperDrawableFlags flags);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_FUNCS_H_
