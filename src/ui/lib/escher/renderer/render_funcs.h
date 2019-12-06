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

    void Bind(CommandBuffer* cb) const { cb->BindVertices(binding_index, buffer, offset, stride); }
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
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_RENDER_FUNCS_H_
