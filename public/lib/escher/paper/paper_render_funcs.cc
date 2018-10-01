// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/paper/paper_render_funcs.h"

#include "lib/escher/renderer/frame.h"
#include "lib/escher/renderer/render_queue_item.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/vk/texture.h"
#include "lib/fxl/logging.h"

namespace {

constexpr uint32_t kMeshAttributeBindingLocation_Position2D = 0;
constexpr uint32_t kMeshAttributeBindingLocation_Position3D = 0;
constexpr uint32_t kMeshAttributeBindingLocation_PositionOffset = 1;
constexpr uint32_t kMeshAttributeBindingLocation_UV = 2;
constexpr uint32_t kMeshAttributeBindingLocation_PerimeterPos = 3;

constexpr uint32_t kMeshUniformBindingIndices_ViewProjectionMatrix[] = {0, 0};

}  // anonymous namespace

namespace escher {

void PaperRenderFuncs::RenderMesh(CommandBuffer* cb,
                                  const RenderQueueContext* context,
                                  const RenderQueueItem* items,
                                  uint32_t instance_count) {
  FXL_DCHECK(cb && items && instance_count > 0);

  auto* mesh_data = static_cast<const MeshObjectData*>(items[0].object_data);

  // Set up per-object state.
  cb->SetShaderProgram(mesh_data->shader_program);
  cb->BindTexture(1, 1, mesh_data->texture);
  cb->BindIndices(mesh_data->index_buffer, mesh_data->index_buffer_offset,
                  vk::IndexType::eUint32);
  for (uint32_t i = 0; i < mesh_data->vertex_binding_count; ++i) {
    auto& b = mesh_data->vertex_bindings[i];
    cb->BindVertices(b.binding_index, b.buffer, b.offset, b.stride);
  }
  for (uint32_t i = 0; i < mesh_data->vertex_attribute_count; ++i) {
    auto& at = mesh_data->vertex_attributes[i];
    cb->SetVertexAttributes(at.binding_index, at.attribute_index, at.format,
                            at.offset);
  }
  for (uint32_t i = 0; i < mesh_data->uniform_binding_count; ++i) {
    auto& b = mesh_data->uniform_bindings[i];
    cb->BindUniformBuffer(b.descriptor_set_index, b.binding_index, b.buffer,
                          b.offset, b.size);
  }

  // For each instance, set up per-instance state and draw.
  for (uint32_t i = 0; i < instance_count; ++i) {
    FXL_DCHECK(items[i].object_data == mesh_data);

    const MeshInstanceData* instance_data =
        static_cast<const MeshInstanceData*>(items[i].instance_data);
    auto& b = instance_data->object_properties;
    cb->BindUniformBuffer(b.descriptor_set_index, b.binding_index, b.buffer,
                          b.offset, b.size);
    cb->DrawIndexed(mesh_data->num_indices);
  }
}

// Helper for PaperRenderFuncs::NewMeshObjectData().
static PaperRenderFuncs::VertexAttributeBinding* FillVertexAttributeBindings(
    PaperRenderFuncs::VertexAttributeBinding* binding, uint32_t binding_index,
    MeshAttributes attributes) {
  using VertexAttributeBinding = PaperRenderFuncs::VertexAttributeBinding;

  if (attributes & MeshAttribute::kPosition2D) {
    *binding++ = VertexAttributeBinding{
        .binding_index = binding_index,
        .attribute_index = kMeshAttributeBindingLocation_Position2D,
        .format = vk::Format::eR32G32Sfloat,
        .offset =
            GetMeshAttributeOffset(attributes, MeshAttribute::kPosition2D)};
  }
  if (attributes & MeshAttribute::kPosition3D) {
    *binding++ = VertexAttributeBinding{
        .binding_index = binding_index,
        .attribute_index = kMeshAttributeBindingLocation_Position3D,
        .format = vk::Format::eR32G32B32Sfloat,
        .offset =
            GetMeshAttributeOffset(attributes, MeshAttribute::kPosition3D)};
  }
  if (attributes & MeshAttribute::kPositionOffset) {
    *binding++ = VertexAttributeBinding{
        .binding_index = binding_index,
        .attribute_index = kMeshAttributeBindingLocation_PositionOffset,
        .format = vk::Format::eR32G32Sfloat,
        .offset =
            GetMeshAttributeOffset(attributes, MeshAttribute::kPositionOffset)};
  }
  if (attributes & MeshAttribute::kUV) {
    *binding++ = VertexAttributeBinding{
        .binding_index = binding_index,
        .attribute_index = kMeshAttributeBindingLocation_UV,
        .format = vk::Format::eR32G32Sfloat,
        .offset = GetMeshAttributeOffset(attributes, MeshAttribute::kUV)};
  }
  if (attributes & MeshAttribute::kPerimeterPos) {
    *binding++ = VertexAttributeBinding{
        .binding_index = binding_index,
        .attribute_index = kMeshAttributeBindingLocation_PerimeterPos,
        .format = vk::Format::eR32G32Sfloat,
        .offset =
            GetMeshAttributeOffset(attributes, MeshAttribute::kPerimeterPos)};
  }
  return binding;
}

PaperRenderFuncs::MeshObjectData* PaperRenderFuncs::NewMeshObjectData(
    const FramePtr& frame, const MeshPtr& mesh, const TexturePtr& texture,
    const ShaderProgramPtr& program,
    const UniformAllocation& view_projection_uniform) {
  FXL_DCHECK(mesh);
  FXL_DCHECK(texture);
  auto& mesh_spec = mesh->spec();

  // TODO(ES-103): avoid reaching in to impl::CommandBuffer for keep-alive.
  frame->command_buffer()->KeepAlive(mesh.get());
  frame->command_buffer()->KeepAlive(texture.get());

  // TODO(ES-104): Replace TakeWaitSemaphore() with something better.
  frame->command_buffer()->TakeWaitSemaphore(
      mesh, vk::PipelineStageFlagBits::eTopOfPipe);

  auto* obj = frame->Allocate<MeshObjectData>();

  obj->index_buffer = mesh->vk_index_buffer();
  obj->index_type = vk::IndexType::eUint32;
  obj->index_buffer_offset = mesh->index_buffer_offset();
  obj->num_indices = mesh->num_indices();

  // Set up vertex buffer bindings.
  obj->vertex_binding_count = mesh_spec.vertex_buffer_count();
  obj->vertex_bindings =
      frame->AllocateMany<VertexBinding>(obj->vertex_binding_count);

  {
    uint32_t binding_count = 0;
    for (uint32_t i = 0; i < VulkanLimits::kNumVertexBuffers; ++i) {
      if (auto& attribute_buffer = mesh->attribute_buffer(i)) {
        // TODO(ES-103): avoid reaching in to impl::CommandBuffer for
        // keep-alive.
        frame->command_buffer()->KeepAlive(attribute_buffer.buffer);

        obj->vertex_bindings[binding_count++] =
            VertexBinding{.binding_index = i,
                          .buffer = attribute_buffer.buffer.get(),
                          .offset = attribute_buffer.offset,
                          .stride = attribute_buffer.stride};
      }
    }
    FXL_DCHECK(binding_count == obj->vertex_binding_count);
  }

  // Set up vertex attribute bindings.
  obj->vertex_attribute_count = mesh_spec.total_attribute_count();
  obj->vertex_attributes =
      frame->AllocateMany<VertexAttributeBinding>(obj->vertex_attribute_count);
  {
    VertexAttributeBinding* current = obj->vertex_attributes;
    for (uint32_t i = 0; i < VulkanLimits::kNumVertexBuffers; ++i) {
      if (mesh_spec.attribute_count(i) > 0) {
        current =
            FillVertexAttributeBindings(current, i, mesh_spec.attributes[i]);
      }
    }

    // Sanity check that we filled in the correct number of attributes.
    FXL_DCHECK(current ==
               (obj->vertex_attributes + obj->vertex_attribute_count));
  }

  obj->uniform_binding_count = 1;
  obj->uniform_bindings = frame->Allocate<UniformBinding>();
  // Use the same view-projection matrix for each mesh.
  obj->uniform_bindings[0] = UniformBinding{
      .descriptor_set_index =
          kMeshUniformBindingIndices_ViewProjectionMatrix[0],
      .binding_index = kMeshUniformBindingIndices_ViewProjectionMatrix[1],
      .buffer = view_projection_uniform.buffer,
      .offset = view_projection_uniform.offset,
      .size = view_projection_uniform.size};

  obj->texture = texture.get();
  obj->shader_program = program.get();

  return obj;
}

}  // namespace escher
