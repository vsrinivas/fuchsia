// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_RENDER_FUNCS_H_
#define SRC_UI_LIB_ESCHER_RENDERER_RENDER_FUNCS_H_

#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/renderer/uniform_binding.h"
#include "src/ui/lib/escher/vk/command_buffer.h"

#include <vulkan/vulkan.hpp>

namespace escher {

struct RenderQueueContext;
struct RenderQueueItem;

class RenderFuncs {
 public:
  // Struct to hold vertex index data.
  struct IndexBinding {
    vk::Buffer index_buffer;
    vk::IndexType index_type;
    uint64_t index_buffer_offset;

    void Bind(CommandBuffer* cb) const {
      cb->BindIndices(index_buffer, index_buffer_offset, index_type);
    }
  };

  // Struct to hold vertex buffer data.
  struct VertexBinding {
    uint32_t binding_index;
    vk::Buffer buffer;
    uint64_t offset;
    uint32_t stride;

    void Bind(CommandBuffer* cb) const { cb->BindVertices(binding_index, buffer, offset, stride); }
  };

  // Struct to hold vertex attribute data.
  struct VertexAttributeBinding {
    uint32_t binding_index;
    uint32_t attribute_index;
    vk::Format format;
    uint32_t offset;

    void Bind(CommandBuffer* cb) const {
      cb->SetVertexAttributes(binding_index, attribute_index, format, offset);
    }
  };

  // Called in PaperRenderer::BeginFrame() to obtain suitable render targets.
  static void ObtainDepthAndMsaaTextures(Escher* escher, const FramePtr& frame,
                                         const ImageInfo& info, uint32_t msaa_sample_count,
                                         vk::Format depth_stencil_format,
                                         TexturePtr& depth_texture, TexturePtr& msaa_texture);

  // Updates or replaces the passed in depth texture (depth_texture_in_out) based on the provided
  // ImageInfo and vk::Format. If the texture pointer is null, a new texture will be allocated.
  static void ObtainDepthTexture(Escher* escher, const bool use_protected_memory,
                                 const ImageInfo& info, vk::Format depth_stencil_format,
                                 TexturePtr& depth_texture_in_out);
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_RENDER_FUNCS_H_
