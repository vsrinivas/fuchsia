// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_render_funcs.h"

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/paper/paper_render_queue_context.h"
#include "src/ui/lib/escher/paper/paper_shader_structs.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/renderer/render_queue_item.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace {

constexpr uint32_t kMeshAttributeBindingLocation_Position2D = 0;
constexpr uint32_t kMeshAttributeBindingLocation_Position3D = 0;
constexpr uint32_t kMeshAttributeBindingLocation_PositionOffset = 1;
constexpr uint32_t kMeshAttributeBindingLocation_UV = 2;
constexpr uint32_t kMeshAttributeBindingLocation_PerimeterPos = 3;
constexpr uint32_t kMeshAttributeBindingLocation_BlendWeight = 4;

}  // anonymous namespace

namespace escher {

void PaperRenderFuncs::MeshData::Bind(CommandBuffer* cb) const {
  TRACE_DURATION("gfx", "PaperRenderFuncs::MeshData::Bind");
  index_binding.Bind(cb);
  for (uint32_t i = 0; i < vertex_binding_count; ++i) {
    vertex_bindings[i].Bind(cb);
  }
  for (uint32_t i = 0; i < vertex_attribute_count; ++i) {
    vertex_attributes[i].Bind(cb);
  }
  for (uint32_t i = 0; i < uniform_binding_count; ++i) {
    uniform_bindings[i].Bind(cb);
  }
  cb->BindTexture(1, 1, texture);
}

void PaperRenderFuncs::RenderMesh(CommandBuffer* cb, const RenderQueueContext* context_in,
                                  const RenderQueueItem* items, uint32_t instance_count) {
  TRACE_DURATION("gfx", "PaperRenderFuncs::RenderMesh");
  FXL_DCHECK(cb && items && instance_count > 0);
  FXL_DCHECK(context_in);
  auto* context = static_cast<const PaperRenderQueueContext*>(context_in);
  auto* mesh_data = static_cast<const MeshData*>(items[0].object_data);
  const PaperRendererDrawMode draw_mode = context->draw_mode();

  uint32_t num_indices = draw_mode == PaperRendererDrawMode::kShadowVolumeGeometry
                             ? mesh_data->num_shadow_volume_indices
                             : mesh_data->num_indices;
  if (num_indices == 0) {
    // The only way this should happen is when rendering shadow-volume geometry
    // for a non-shadow-caster.
    FXL_DCHECK(draw_mode == PaperRendererDrawMode::kShadowVolumeGeometry);
    return;
  }

  // Set up per-object state.
  mesh_data->Bind(cb);

  // TODO(ES-158): this assumes that all meshes in this render-queue pass are
  // drawn exactly the same way.  We will need something better soon.
  cb->SetShaderProgram(context->shader_program(), mesh_data->texture->sampler()->is_immutable()
                                                      ? mesh_data->texture->sampler()
                                                      : nullptr);

  // For each instance, set up per-instance state and draw.
  for (uint32_t i = 0; i < instance_count; ++i) {
    FXL_DCHECK(items[i].object_data == mesh_data);

    const MeshDrawData* instance_data = static_cast<const MeshDrawData*>(items[i].instance_data);

    if (draw_mode == PaperRendererDrawMode::kShadowVolumeGeometry &&
        (instance_data->flags & PaperDrawableFlagBits::kDisableShadowCasting)) {
      // This instance shouldn't draw shadows; continue to the next one.
      continue;
    }

    auto& b = instance_data->object_properties;
    cb->BindUniformBuffer(b.descriptor_set_index, b.binding_index, b.buffer, b.offset, b.size);
    cb->DrawIndexed(num_indices);
  }
}

// Helper for PaperRenderFuncs::NewMeshData().
static PaperRenderFuncs::VertexAttributeBinding* FillVertexAttributeBindings(
    PaperRenderFuncs::VertexAttributeBinding* binding, uint32_t binding_index,
    MeshAttributes attributes) {
  using VertexAttributeBinding = PaperRenderFuncs::VertexAttributeBinding;

  if (attributes & MeshAttribute::kPosition2D) {
    *binding++ = VertexAttributeBinding{
        .binding_index = binding_index,
        .attribute_index = kMeshAttributeBindingLocation_Position2D,
        .format = vk::Format::eR32G32Sfloat,
        .offset = GetMeshAttributeOffset(attributes, MeshAttribute::kPosition2D)};
  }
  if (attributes & MeshAttribute::kPosition3D) {
    *binding++ = VertexAttributeBinding{
        .binding_index = binding_index,
        .attribute_index = kMeshAttributeBindingLocation_Position3D,
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = GetMeshAttributeOffset(attributes, MeshAttribute::kPosition3D)};
  }
  if (attributes & MeshAttribute::kPositionOffset) {
    *binding++ = VertexAttributeBinding{
        .binding_index = binding_index,
        .attribute_index = kMeshAttributeBindingLocation_PositionOffset,
        .format = vk::Format::eR32G32Sfloat,
        .offset = GetMeshAttributeOffset(attributes, MeshAttribute::kPositionOffset)};
  }
  if (attributes & MeshAttribute::kUV) {
    *binding++ =
        VertexAttributeBinding{.binding_index = binding_index,
                               .attribute_index = kMeshAttributeBindingLocation_UV,
                               .format = vk::Format::eR32G32Sfloat,
                               .offset = GetMeshAttributeOffset(attributes, MeshAttribute::kUV)};
  }
  if (attributes & MeshAttribute::kPerimeterPos) {
    *binding++ = VertexAttributeBinding{
        .binding_index = binding_index,
        .attribute_index = kMeshAttributeBindingLocation_PerimeterPos,
        .format = vk::Format::eR32G32Sfloat,
        .offset = GetMeshAttributeOffset(attributes, MeshAttribute::kPerimeterPos)};
  }
  if (attributes & MeshAttribute::kBlendWeight1) {
    *binding++ = VertexAttributeBinding{
        .binding_index = binding_index,
        .attribute_index = kMeshAttributeBindingLocation_BlendWeight,
        .format = vk::Format::eR32Sfloat,
        .offset = GetMeshAttributeOffset(attributes, MeshAttribute::kBlendWeight1)};
  }
  return binding;
}

PaperRenderFuncs::MeshData* PaperRenderFuncs::NewMeshData(const FramePtr& frame, Mesh* mesh,
                                                          const TexturePtr& texture,
                                                          uint32_t num_indices,
                                                          uint32_t num_shadow_volume_indices) {
  TRACE_DURATION("gfx", "PaperRenderFuncs::NewMeshData");
  FXL_DCHECK(mesh);
  FXL_DCHECK(texture);
  auto& mesh_spec = mesh->spec();

  // TODO(ES-103): avoid reaching in to impl::CommandBuffer for keep-alive.
  frame->cmds()->KeepAlive(mesh);
  frame->cmds()->KeepAlive(texture.get());

  auto* obj = frame->Allocate<MeshData>();

  obj->index_binding.index_buffer = mesh->vk_index_buffer();
  obj->index_binding.index_type = MeshSpec::IndexTypeEnum;
  obj->index_binding.index_buffer_offset = mesh->index_buffer_offset();
  obj->num_indices = num_indices;
  obj->num_shadow_volume_indices = num_shadow_volume_indices;

  // Set up vertex buffer bindings.
  obj->vertex_binding_count = mesh_spec.vertex_buffer_count();
  obj->vertex_bindings = frame->AllocateMany<VertexBinding>(obj->vertex_binding_count);

  {
    uint32_t binding_count = 0;
    for (uint32_t i = 0; i < VulkanLimits::kNumVertexBuffers; ++i) {
      if (auto& attribute_buffer = mesh->attribute_buffer(i)) {
        // TODO(ES-103): avoid reaching in to impl::CommandBuffer for
        // keep-alive.
        frame->cmds()->KeepAlive(attribute_buffer.buffer);

        obj->vertex_bindings[binding_count++] =
            VertexBinding{.binding_index = i,
                          .buffer = attribute_buffer.buffer->vk(),
                          .offset = attribute_buffer.offset,
                          .stride = attribute_buffer.stride};
      }
    }
    FXL_DCHECK(binding_count == obj->vertex_binding_count);
  }

  // Set up vertex attribute bindings.
  obj->vertex_attribute_count = mesh_spec.total_attribute_count();
  obj->vertex_attributes = frame->AllocateMany<VertexAttributeBinding>(obj->vertex_attribute_count);
  {
    VertexAttributeBinding* current = obj->vertex_attributes;
    for (uint32_t i = 0; i < VulkanLimits::kNumVertexBuffers; ++i) {
      if (mesh_spec.attribute_count(i) > 0) {
        current = FillVertexAttributeBindings(current, i, mesh_spec.attributes[i]);
      }
    }

    // Sanity check that we filled in the correct number of attributes.
    FXL_DCHECK(current == (obj->vertex_attributes + obj->vertex_attribute_count));
  }

  obj->uniform_binding_count = 0;
  obj->texture = texture.get();

  return obj;
}

PaperRenderFuncs::MeshDrawData* PaperRenderFuncs::NewMeshDrawData(const FramePtr& frame,
                                                                  const mat4& transform,
                                                                  const vec4& color,
                                                                  PaperDrawableFlags flags) {
  MeshDrawData* draw_data = frame->Allocate<MeshDrawData>();

  auto writable_binding = NewPaperShaderUniformBinding<PaperShaderMeshInstance>(frame);
  writable_binding.first->model_transform = transform;
  writable_binding.first->color = color;
  // TODO(ES-152): populate field for vertex-shader clip-planes.

  draw_data->object_properties = writable_binding.second;
  draw_data->flags = flags;

  return draw_data;
}

}  // namespace escher
