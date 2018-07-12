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

constexpr uint32_t kMeshVertexBufferBindingIndex = 0;
constexpr uint32_t kMeshAttributeBindingIndices_Position2D[] = {
    kMeshVertexBufferBindingIndex, 0};
constexpr uint32_t kMeshAttributeBindingIndices_Position3D[] = {
    kMeshVertexBufferBindingIndex, 0};
constexpr uint32_t kMeshAttributeBindingIndices_PositionOffset[] = {
    kMeshVertexBufferBindingIndex, 1};
constexpr uint32_t kMeshAttributeBindingIndices_UV[] = {
    kMeshVertexBufferBindingIndex, 2};
constexpr uint32_t kMeshAttributeBindingIndices_PerimeterPos[] = {
    kMeshVertexBufferBindingIndex, 3};

constexpr uint32_t kMeshUniformBindingIndices_ViewProjectionMatrix[] = {0, 0};

}  // anonymous namespace

namespace escher {

void PaperRenderFuncs::RenderMesh(CommandBuffer* cb,
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

PaperRenderFuncs::MeshObjectData* PaperRenderFuncs::NewMeshObjectData(
    const FramePtr& frame, const MeshPtr& mesh, const TexturePtr& texture,
    const ShaderProgramPtr& program,
    const UniformAllocation& view_projection_uniform) {
  FXL_DCHECK(mesh);
  FXL_DCHECK(texture);

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

  obj->vertex_binding_count = 1;
  obj->vertex_bindings = frame->Allocate<VertexBinding>();
  auto& vertex_buffer = mesh->vertex_buffer();
  // TODO(ES-103): avoid reaching in to impl::CommandBuffer for keep-alive.
  frame->command_buffer()->KeepAlive(vertex_buffer);
  obj->vertex_bindings[0] =
      VertexBinding{.binding_index = kMeshVertexBufferBindingIndex,
                    .buffer = vertex_buffer.get(),
                    .offset = mesh->vertex_buffer_offset(),
                    .stride = mesh->spec().GetStride()};

  // Set up vertex attributes based on MeshSpec.
  auto& mesh_spec = mesh->spec();
  size_t num_attributes = mesh_spec.GetNumAttributes();
  obj->vertex_attribute_count = num_attributes;
  obj->vertex_attributes =
      frame->AllocateMany<VertexAttributeBinding>(num_attributes);
  size_t attribute_index = 0;
  if (mesh_spec.flags & MeshAttribute::kPosition2D) {
    obj->vertex_attributes[attribute_index++] = VertexAttributeBinding{
        .binding_index = kMeshAttributeBindingIndices_Position2D[0],
        .attribute_index = kMeshAttributeBindingIndices_Position2D[1],
        .format = vk::Format::eR32G32Sfloat,
        .offset = mesh->spec().GetAttributeOffset(MeshAttribute::kPosition2D)};
  }
  if (mesh_spec.flags & MeshAttribute::kPosition3D) {
    obj->vertex_attributes[attribute_index++] = VertexAttributeBinding{
        .binding_index = kMeshAttributeBindingIndices_Position3D[0],
        .attribute_index = kMeshAttributeBindingIndices_Position3D[1],
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = mesh->spec().GetAttributeOffset(MeshAttribute::kPosition3D)};
  }
  if (mesh_spec.flags & MeshAttribute::kPositionOffset) {
    obj->vertex_attributes[attribute_index++] = VertexAttributeBinding{
        .binding_index = kMeshAttributeBindingIndices_PositionOffset[0],
        .attribute_index = kMeshAttributeBindingIndices_PositionOffset[1],
        .format = vk::Format::eR32G32Sfloat,
        .offset =
            mesh->spec().GetAttributeOffset(MeshAttribute::kPositionOffset)};
  }
  if (mesh_spec.flags & MeshAttribute::kUV) {
    obj->vertex_attributes[attribute_index++] = VertexAttributeBinding{
        .binding_index = kMeshAttributeBindingIndices_UV[0],
        .attribute_index = kMeshAttributeBindingIndices_UV[1],
        .format = vk::Format::eR32G32Sfloat,
        .offset = mesh->spec().GetAttributeOffset(MeshAttribute::kUV)};
  }
  if (mesh_spec.flags & MeshAttribute::kPerimeterPos) {
    obj->vertex_attributes[attribute_index++] = VertexAttributeBinding{
        .binding_index = kMeshAttributeBindingIndices_PerimeterPos[0],
        .attribute_index = kMeshAttributeBindingIndices_PerimeterPos[1],
        .format = vk::Format::eR32G32Sfloat,
        .offset =
            mesh->spec().GetAttributeOffset(MeshAttribute::kPerimeterPos)};
  }
  FXL_DCHECK(attribute_index == num_attributes);  // sanity check

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
