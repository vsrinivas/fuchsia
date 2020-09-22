// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_FUNCS_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_FUNCS_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/paper/paper_drawable_flags.h"
#include "src/ui/lib/escher/paper/paper_readme.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"

#include <vulkan/vulkan.hpp>

namespace escher {

struct RenderQueueContext;
struct RenderQueueItem;

// Helper functions and structs used by e.g. PaperRenderQueue to produce
// RenderQueueItems for the RenderQueues that it encapsulates.
class PaperRenderFuncs : public RenderFuncs {
 public:
  // Matches signature of |RenderQueueItem::RenderFunc|.  Expects the items'
  // |object_data| and |instance_data| to be of type |MeshData| and
  // |MeshDrawData|, respectively; these types are defined below.
  static void RenderMesh(CommandBuffer* cb, const RenderQueueContext* context,
                         const RenderQueueItem* items, uint32_t instance_count);

  // Struct intended to be used as the |instance_data| of a |RenderQueueItem|.
  struct MeshDrawData {
    UniformBinding object_properties;
    PaperDrawableFlags flags;
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

    // TODO(fxbug.dev/7250): Texture bindings, not just one texture hardcoded to (1,1).
    Texture* texture;

    void Bind(CommandBuffer* cb) const;
  };

  // These are the vertex shader binding locations where each of these attributes are expected by
  // the PaperRenderer shader code.  For example, see escher/shaders/paper/common/use.glsl.
  constexpr static MeshAttributeBindingLocations kMeshAttributeBindingLocations = {
      .position_2d = 0,
      .position_3d = 0,
      .position_offset = 1,
      .uv = 2,
      .perimeter_pos = 3,
      .blend_weight1 = 4};

  // Helper function for allocating/populating a |MeshData|.  Both CPU and
  // uniform GPU memory is allocated using per-Frame allocators.
  static PaperRenderFuncs::MeshData* NewMeshData(const FramePtr& frame, Mesh* mesh,
                                                 const TexturePtr& texture, uint32_t num_indices,
                                                 uint32_t num_shadow_volume_indices);

  // Helper function for allocating/populating a |MeshDrawData|.  Both CPU and
  // uniform GPU memory is allocated using per-Frame allocators.
  static PaperRenderFuncs::MeshDrawData* NewMeshDrawData(const FramePtr& frame,
                                                         const mat4& transform, const vec4& color,
                                                         PaperDrawableFlags flags);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_FUNCS_H_
