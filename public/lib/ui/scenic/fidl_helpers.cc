// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/fidl_helpers.h"

#include <array>

#include "lib/fxl/logging.h"

namespace scenic_lib {

// TODO(mikejurka): this should be in an images util file
bool ImageInfoEquals(const images::ImageInfo& a, const images::ImageInfo& b) {
  return a.transform == b.transform && a.width == b.width &&
         a.height == b.height && a.stride == b.stride &&
         a.pixel_format == b.pixel_format && a.color_space == b.color_space &&
         a.tiling == b.tiling && a.alpha_format == b.alpha_format;
}
ui::Command NewCommand(gfx::Command command) {
  ui::Command scenic_command;
  scenic_command.set_gfx(std::move(command));
  return scenic_command;
}

// Helper function for all resource creation functions.
static gfx::Command NewCreateResourceCommand(uint32_t id,
                                             gfx::ResourceArgs resource) {
  gfx::CreateResourceCommand create_resource;
  create_resource.id = id;
  create_resource.resource = std::move(resource);

  gfx::Command command;
  command.set_create_resource(std::move(create_resource));

  return command;
}

gfx::Command NewCreateMemoryCommand(uint32_t id,
                                    zx::vmo vmo,
                                    images::MemoryType memory_type) {
  gfx::MemoryArgs memory;
  memory.vmo = std::move(vmo);
  memory.memory_type = memory_type;

  gfx::ResourceArgs resource;
  resource.set_memory(std::move(memory));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateImageCommand(uint32_t id,
                                   uint32_t memory_id,
                                   uint32_t memory_offset,
                                   images::ImageInfo info) {
  gfx::ImageArgs image;
  image.memory_id = memory_id;
  image.memory_offset = memory_offset;
  image.info = std::move(info);

  gfx::ResourceArgs resource;
  resource.set_image(std::move(image));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateImagePipeCommand(
    uint32_t id,
    ::fidl::InterfaceRequest<images::ImagePipe> request) {
  gfx::ImagePipeArgs image_pipe;
  image_pipe.image_pipe_request = std::move(request);

  gfx::ResourceArgs resource;
  resource.set_image_pipe(std::move(image_pipe));
  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateImageCommand(uint32_t id,
                                   uint32_t memory_id,
                                   uint32_t memory_offset,
                                   images::PixelFormat format,
                                   images::ColorSpace color_space,
                                   images::Tiling tiling,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t stride) {
  images::ImageInfo info;
  info.pixel_format = format;
  info.color_space = color_space;
  info.tiling = tiling;
  info.width = width;
  info.height = height;
  info.stride = stride;
  return NewCreateImageCommand(id, memory_id, memory_offset, std::move(info));
}

gfx::Command NewCreateBufferCommand(uint32_t id,
                                    uint32_t memory_id,
                                    uint32_t memory_offset,
                                    uint32_t num_bytes) {
  gfx::BufferArgs buffer;
  buffer.memory_id = memory_id;
  buffer.memory_offset = memory_offset;
  buffer.num_bytes = num_bytes;

  gfx::ResourceArgs resource;
  resource.set_buffer(std::move(buffer));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateDisplayCompositorCommand(uint32_t id) {
  gfx::DisplayCompositorArgs display_compositor;

  gfx::ResourceArgs resource;
  resource.set_display_compositor(std::move(display_compositor));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateLayerStackCommand(uint32_t id) {
  gfx::LayerStackArgs layer_stack;

  gfx::ResourceArgs resource;
  resource.set_layer_stack(std::move(layer_stack));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateLayerCommand(uint32_t id) {
  gfx::LayerArgs layer;

  gfx::ResourceArgs resource;
  resource.set_layer(std::move(layer));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateSceneCommand(uint32_t id) {
  gfx::SceneArgs scene;

  gfx::ResourceArgs resource;
  resource.set_scene(std::move(scene));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateCameraCommand(uint32_t id, uint32_t scene_id) {
  gfx::CameraArgs camera;
  camera.scene_id = scene_id;

  gfx::ResourceArgs resource;
  resource.set_camera(std::move(camera));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateStereoCameraCommand(uint32_t id, uint32_t scene_id) {
  gfx::StereoCameraArgs stereo_camera;
  stereo_camera.scene_id = scene_id;

  gfx::ResourceArgs resource;
  resource.set_stereo_camera(std::move(stereo_camera));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateRendererCommand(uint32_t id) {
  gfx::RendererArgs renderer;
  gfx::ResourceArgs resource;
  resource.set_renderer(std::move(renderer));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateAmbientLightCommand(uint32_t id) {
  gfx::AmbientLightArgs ambient_light;

  gfx::ResourceArgs resource;
  resource.set_ambient_light(std::move(ambient_light));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateDirectionalLightCommand(uint32_t id) {
  gfx::DirectionalLightArgs directional_light;

  gfx::ResourceArgs resource;
  resource.set_directional_light(std::move(directional_light));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateCircleCommand(uint32_t id, float radius) {
  gfx::Value radius_value;
  radius_value.set_vector1(radius);

  gfx::CircleArgs circle;
  circle.radius = std::move(radius_value);

  gfx::ResourceArgs resource;
  resource.set_circle(std::move(circle));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateRectangleCommand(uint32_t id, float width, float height) {
  gfx::Value width_value;
  width_value.set_vector1(width);

  gfx::Value height_value;
  height_value.set_vector1(height);

  gfx::RectangleArgs rectangle;
  rectangle.width = std::move(width_value);
  rectangle.height = std::move(height_value);

  gfx::ResourceArgs resource;
  resource.set_rectangle(std::move(rectangle));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateRoundedRectangleCommand(uint32_t id,
                                              float width,
                                              float height,
                                              float top_left_radius,
                                              float top_right_radius,
                                              float bottom_right_radius,
                                              float bottom_left_radius) {
  gfx::Value width_value;
  width_value.set_vector1(width);

  gfx::Value height_value;
  height_value.set_vector1(height);

  gfx::Value top_left_radius_value;
  top_left_radius_value.set_vector1(top_left_radius);

  gfx::Value top_right_radius_value;
  top_right_radius_value.set_vector1(top_right_radius);

  gfx::Value bottom_right_radius_value;
  bottom_right_radius_value.set_vector1(bottom_right_radius);

  gfx::Value bottom_left_radius_value;
  bottom_left_radius_value.set_vector1(bottom_left_radius);

  gfx::RoundedRectangleArgs rectangle;
  rectangle.width = std::move(width_value);
  rectangle.height = std::move(height_value);
  rectangle.top_left_radius = std::move(top_left_radius_value);
  rectangle.top_right_radius = std::move(top_right_radius_value);
  rectangle.bottom_right_radius = std::move(bottom_right_radius_value);
  rectangle.bottom_left_radius = std::move(bottom_left_radius_value);

  gfx::ResourceArgs resource;
  resource.set_rounded_rectangle(std::move(rectangle));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateVarCircleCommand(uint32_t id,
                                       uint32_t radius_var_id,
                                       uint32_t height_var_id) {
  gfx::Value radius_value;
  radius_value.set_variable_id(radius_var_id);

  gfx::CircleArgs circle;
  circle.radius = std::move(radius_value);

  gfx::ResourceArgs resource;
  resource.set_circle(std::move(circle));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateVarRectangleCommand(uint32_t id,
                                          uint32_t width_var_id,
                                          uint32_t height_var_id) {
  gfx::Value width_value;
  width_value.set_variable_id(width_var_id);

  gfx::Value height_value;
  height_value.set_variable_id(height_var_id);

  gfx::RectangleArgs rectangle;
  rectangle.width = std::move(width_value);
  rectangle.height = std::move(height_value);

  gfx::ResourceArgs resource;
  resource.set_rectangle(std::move(rectangle));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateVarRoundedRectangleCommand(
    uint32_t id,
    uint32_t width_var_id,
    uint32_t height_var_id,
    uint32_t top_left_radius_var_id,
    uint32_t top_right_radius_var_id,
    uint32_t bottom_left_radius_var_id,
    uint32_t bottom_right_radius_var_id) {
  gfx::Value width_value;
  width_value.set_variable_id(width_var_id);

  gfx::Value height_value;
  height_value.set_variable_id(height_var_id);

  gfx::Value top_left_radius_value;
  top_left_radius_value.set_variable_id(top_left_radius_var_id);

  gfx::Value top_right_radius_value;
  top_right_radius_value.set_variable_id(top_right_radius_var_id);

  gfx::Value bottom_left_radius_value;
  bottom_left_radius_value.set_variable_id(bottom_left_radius_var_id);

  gfx::Value bottom_right_radius_value;
  bottom_right_radius_value.set_variable_id(bottom_right_radius_var_id);

  gfx::RoundedRectangleArgs rectangle;
  rectangle.width = std::move(width_value);
  rectangle.height = std::move(height_value);
  rectangle.top_left_radius = std::move(top_left_radius_value);
  rectangle.top_right_radius = std::move(top_right_radius_value);
  rectangle.bottom_left_radius = std::move(bottom_left_radius_value);
  rectangle.bottom_right_radius = std::move(bottom_right_radius_value);

  gfx::ResourceArgs resource;
  resource.set_rounded_rectangle(std::move(rectangle));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateMeshCommand(uint32_t id) {
  gfx::MeshArgs mesh;

  gfx::ResourceArgs resource;
  resource.set_mesh(std::move(mesh));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateMaterialCommand(uint32_t id) {
  gfx::MaterialArgs material;

  gfx::ResourceArgs resource;
  resource.set_material(std::move(material));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateClipNodeCommand(uint32_t id) {
  gfx::ClipNodeArgs node;

  gfx::ResourceArgs resource;
  resource.set_clip_node(std::move(node));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateEntityNodeCommand(uint32_t id) {
  gfx::EntityNodeArgs node;

  gfx::ResourceArgs resource;
  resource.set_entity_node(std::move(node));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateShapeNodeCommand(uint32_t id) {
  gfx::ShapeNodeArgs node;

  gfx::ResourceArgs resource;
  resource.set_shape_node(std::move(node));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewCreateVariableCommand(uint32_t id, gfx::Value value) {
  gfx::VariableArgs variable;
  switch (value.Which()) {
    case ::gfx::Value::Tag::kVector1:
      variable.type = gfx::ValueType::kVector1;
      break;
    case ::gfx::Value::Tag::kVector2:
      variable.type = gfx::ValueType::kVector2;
      break;
    case ::gfx::Value::Tag::kVector3:
      variable.type = gfx::ValueType::kVector3;
      break;
    case ::gfx::Value::Tag::kVector4:
      variable.type = gfx::ValueType::kVector4;
      break;
    case gfx::Value::Tag::kMatrix4x4:
      variable.type = gfx::ValueType::kMatrix4;
      break;
    case gfx::Value::Tag::kColorRgba:
      variable.type = gfx::ValueType::kColorRgba;
      break;
    case gfx::Value::Tag::kColorRgb:
      variable.type = gfx::ValueType::kColorRgb;
      break;
    case gfx::Value::Tag::kDegrees:
      variable.type = gfx::ValueType::kVector1;
      break;
    case gfx::Value::Tag::kTransform:
      variable.type = gfx::ValueType::kFactoredTransform;
      break;
    case gfx::Value::Tag::kQuaternion:
      variable.type = gfx::ValueType::kQuaternion;
      break;
    case gfx::Value::Tag::kVariableId:
      // A variable's initial value cannot be another variable.
      return gfx::Command();
    case gfx::Value::Tag::Invalid:
      return gfx::Command();
  }
  variable.initial_value = std::move(value);

  gfx::ResourceArgs resource;
  resource.set_variable(std::move(variable));

  return NewCreateResourceCommand(id, std::move(resource));
}

gfx::Command NewReleaseResourceCommand(uint32_t id) {
  gfx::ReleaseResourceCommand release_resource;
  release_resource.id = id;

  gfx::Command command;
  command.set_release_resource(std::move(release_resource));

  return command;
}

gfx::Command NewExportResourceCommand(uint32_t resource_id,
                                      zx::eventpair export_token) {
  FXL_DCHECK(export_token);

  gfx::ExportResourceCommand export_resource;
  export_resource.id = resource_id;
  export_resource.token = std::move(export_token);

  gfx::Command command;
  command.set_export_resource(std::move(export_resource));

  return command;
}

gfx::Command NewImportResourceCommand(uint32_t resource_id,
                                      gfx::ImportSpec spec,
                                      zx::eventpair import_token) {
  FXL_DCHECK(import_token);

  gfx::ImportResourceCommand import_resource;
  import_resource.id = resource_id;
  import_resource.token = std::move(import_token);
  import_resource.spec = spec;

  gfx::Command command;
  command.set_import_resource(std::move(import_resource));

  return command;
}

gfx::Command NewExportResourceCommandAsRequest(
    uint32_t resource_id,
    zx::eventpair* out_import_token) {
  FXL_DCHECK(out_import_token);
  FXL_DCHECK(!*out_import_token);

  zx::eventpair export_token;
  zx_status_t status =
      zx::eventpair::create(0u, &export_token, out_import_token);
  FXL_CHECK(status == ZX_OK) << "event pair create failed: status=" << status;
  return NewExportResourceCommand(resource_id, std::move(export_token));
}

gfx::Command NewImportResourceCommandAsRequest(
    uint32_t resource_id,
    gfx::ImportSpec import_spec,
    zx::eventpair* out_export_token) {
  FXL_DCHECK(out_export_token);
  FXL_DCHECK(!*out_export_token);

  zx::eventpair import_token;
  zx_status_t status =
      zx::eventpair::create(0u, &import_token, out_export_token);
  FXL_CHECK(status == ZX_OK) << "event pair create failed: status=" << status;
  return NewImportResourceCommand(resource_id, import_spec,
                                  std::move(import_token));
}

gfx::Command NewAddChildCommand(uint32_t node_id, uint32_t child_id) {
  gfx::AddChildCommand add_child;
  add_child.node_id = node_id;
  add_child.child_id = child_id;

  gfx::Command command;
  command.set_add_child(std::move(add_child));

  return command;
}

gfx::Command NewAddPartCommand(uint32_t node_id, uint32_t part_id) {
  gfx::AddPartCommand add_part;
  add_part.node_id = node_id;
  add_part.part_id = part_id;

  gfx::Command command;
  command.set_add_part(std::move(add_part));

  return command;
}

gfx::Command NewDetachCommand(uint32_t id) {
  gfx::DetachCommand detach;
  detach.id = id;

  gfx::Command command;
  command.set_detach(std::move(detach));

  return command;
}

gfx::Command NewDetachChildrenCommand(uint32_t node_id) {
  gfx::DetachChildrenCommand detach_children;
  detach_children.node_id = node_id;

  gfx::Command command;
  command.set_detach_children(std::move(detach_children));

  return command;
}

gfx::Command NewSetTranslationCommand(uint32_t node_id,
                                      const float translation[3]) {
  gfx::SetTranslationCommand set_translation;
  set_translation.id = node_id;
  set_translation.value = NewVector3Value(translation);

  gfx::Command command;
  command.set_set_translation(std::move(set_translation));

  return command;
}

gfx::Command NewSetTranslationCommand(uint32_t node_id, uint32_t variable_id) {
  gfx::SetTranslationCommand set_translation;
  set_translation.id = node_id;
  set_translation.value = NewVector3Value(variable_id);

  gfx::Command command;
  command.set_set_translation(std::move(set_translation));

  return command;
}

gfx::Command NewSetScaleCommand(uint32_t node_id, const float scale[3]) {
  gfx::SetScaleCommand set_scale;
  set_scale.id = node_id;
  set_scale.value = NewVector3Value(scale);

  gfx::Command command;
  command.set_set_scale(std::move(set_scale));

  return command;
}

gfx::Command NewSetScaleCommand(uint32_t node_id, uint32_t variable_id) {
  gfx::SetScaleCommand set_scale;
  set_scale.id = node_id;
  set_scale.value = NewVector3Value(variable_id);

  gfx::Command command;
  command.set_set_scale(std::move(set_scale));

  return command;
}

gfx::Command NewSetRotationCommand(uint32_t node_id,
                                   const float quaternion[4]) {
  gfx::SetRotationCommand set_rotation;
  set_rotation.id = node_id;
  set_rotation.value = NewQuaternionValue(quaternion);

  gfx::Command command;
  command.set_set_rotation(std::move(set_rotation));

  return command;
}

gfx::Command NewSetRotationCommand(uint32_t node_id, uint32_t variable_id) {
  gfx::SetRotationCommand set_rotation;
  set_rotation.id = node_id;
  set_rotation.value = NewQuaternionValue(variable_id);

  gfx::Command command;
  command.set_set_rotation(std::move(set_rotation));

  return command;
}

gfx::Command NewSetAnchorCommand(uint32_t node_id, const float anchor[3]) {
  gfx::SetAnchorCommand set_anchor;
  set_anchor.id = node_id;
  set_anchor.value = NewVector3Value(anchor);

  gfx::Command command;
  command.set_set_anchor(std::move(set_anchor));

  return command;
}

gfx::Command NewSetAnchorCommand(uint32_t node_id, uint32_t variable_id) {
  gfx::SetAnchorCommand set_anchor;
  set_anchor.id = node_id;
  set_anchor.value = NewVector3Value(variable_id);

  gfx::Command command;
  command.set_set_anchor(std::move(set_anchor));

  return command;
}

gfx::Command NewSetShapeCommand(uint32_t node_id, uint32_t shape_id) {
  gfx::SetShapeCommand set_shape;
  set_shape.node_id = node_id;
  set_shape.shape_id = shape_id;

  gfx::Command command;
  command.set_set_shape(std::move(set_shape));

  return command;
}

gfx::Command NewSetMaterialCommand(uint32_t node_id, uint32_t material_id) {
  gfx::SetMaterialCommand set_material;
  set_material.node_id = node_id;
  set_material.material_id = material_id;

  gfx::Command command;
  command.set_set_material(std::move(set_material));

  return command;
}

gfx::Command NewSetClipCommand(uint32_t node_id,
                               uint32_t clip_id,
                               bool clip_to_self) {
  gfx::SetClipCommand set_clip;
  set_clip.node_id = node_id;
  set_clip.clip_id = clip_id;
  set_clip.clip_to_self = clip_to_self;

  gfx::Command command;
  command.set_set_clip(std::move(set_clip));

  return command;
}

gfx::Command NewSetTagCommand(uint32_t node_id, uint32_t tag_value) {
  gfx::SetTagCommand set_tag;
  set_tag.node_id = node_id;
  set_tag.tag_value = tag_value;

  gfx::Command command;
  command.set_set_tag(std::move(set_tag));

  return command;
}

gfx::Command NewSetHitTestBehaviorCommand(
    uint32_t node_id,
    gfx::HitTestBehavior hit_test_behavior) {
  gfx::SetHitTestBehaviorCommand set_hit_test_behavior;
  set_hit_test_behavior.node_id = node_id;
  set_hit_test_behavior.hit_test_behavior = hit_test_behavior;

  gfx::Command command;
  command.set_set_hit_test_behavior(std::move(set_hit_test_behavior));

  return command;
}

gfx::Command NewSetCameraCommand(uint32_t renderer_id, uint32_t camera_id) {
  gfx::SetCameraCommand set_camera;
  set_camera.renderer_id = renderer_id;
  set_camera.camera_id = camera_id;

  gfx::Command command;
  command.set_set_camera(std::move(set_camera));
  return command;
}

gfx::Command NewSetTextureCommand(uint32_t material_id, uint32_t texture_id) {
  gfx::SetTextureCommand set_texture;
  set_texture.material_id = material_id;
  set_texture.texture_id = texture_id;

  gfx::Command command;
  command.set_set_texture(std::move(set_texture));
  return command;
}

gfx::Command NewSetColorCommand(uint32_t material_id,
                                uint8_t red,
                                uint8_t green,
                                uint8_t blue,
                                uint8_t alpha) {
  gfx::ColorRgbaValue color;
  color.value.red = red;
  color.value.green = green;
  color.value.blue = blue;
  color.value.alpha = alpha;
  color.variable_id = 0;
  gfx::SetColorCommand set_color;
  set_color.material_id = material_id;
  set_color.color = std::move(color);

  gfx::Command command;
  command.set_set_color(std::move(set_color));

  return command;
}

gfx::MeshVertexFormat NewMeshVertexFormat(gfx::ValueType position_type,
                                          gfx::ValueType normal_type,
                                          gfx::ValueType tex_coord_type) {
  gfx::MeshVertexFormat vertex_format;
  vertex_format.position_type = position_type;
  vertex_format.normal_type = normal_type;
  vertex_format.tex_coord_type = tex_coord_type;
  return vertex_format;
}

gfx::Command NewBindMeshBuffersCommand(uint32_t mesh_id,
                                       uint32_t index_buffer_id,
                                       gfx::MeshIndexFormat index_format,
                                       uint64_t index_offset,
                                       uint32_t index_count,
                                       uint32_t vertex_buffer_id,
                                       gfx::MeshVertexFormat vertex_format,
                                       uint64_t vertex_offset,
                                       uint32_t vertex_count,
                                       const float bounding_box_min[3],
                                       const float bounding_box_max[3]) {
  gfx::BindMeshBuffersCommand bind_mesh_buffers;
  bind_mesh_buffers.mesh_id = mesh_id;
  bind_mesh_buffers.index_buffer_id = index_buffer_id;
  bind_mesh_buffers.index_format = index_format;
  bind_mesh_buffers.index_offset = index_offset;
  bind_mesh_buffers.index_count = index_count;
  bind_mesh_buffers.vertex_buffer_id = vertex_buffer_id;
  bind_mesh_buffers.vertex_format = std::move(vertex_format);
  bind_mesh_buffers.vertex_offset = vertex_offset;
  bind_mesh_buffers.vertex_count = vertex_count;
  auto& bbox = bind_mesh_buffers.bounding_box;
  bbox.min.x = bounding_box_min[0];
  bbox.min.y = bounding_box_min[1];
  bbox.min.z = bounding_box_min[2];
  bbox.max.x = bounding_box_max[0];
  bbox.max.y = bounding_box_max[1];
  bbox.max.z = bounding_box_max[2];

  gfx::Command command;
  command.set_bind_mesh_buffers(std::move(bind_mesh_buffers));

  return command;
}

gfx::Command NewAddLayerCommand(uint32_t layer_stack_id, uint32_t layer_id) {
  gfx::AddLayerCommand add_layer;
  add_layer.layer_stack_id = layer_stack_id;
  add_layer.layer_id = layer_id;

  gfx::Command command;
  command.set_add_layer(std::move(add_layer));
  return command;
}

gfx::Command NewRemoveLayerCommand(uint32_t layer_stack_id, uint32_t layer_id) {
  gfx::RemoveLayerCommand remove_layer;
  remove_layer.layer_stack_id = layer_stack_id;
  remove_layer.layer_id = layer_id;

  gfx::Command command;
  command.set_remove_layer(std::move(remove_layer));
  return command;
}

gfx::Command NewRemoveAllLayersCommand(uint32_t layer_stack_id) {
  gfx::RemoveAllLayersCommand remove_all_layers;
  remove_all_layers.layer_stack_id = layer_stack_id;

  gfx::Command command;
  command.set_remove_all_layers(std::move(remove_all_layers));
  return command;
}

gfx::Command NewSetLayerStackCommand(uint32_t compositor_id,
                                     uint32_t layer_stack_id) {
  gfx::SetLayerStackCommand set_layer_stack;
  set_layer_stack.compositor_id = compositor_id;
  set_layer_stack.layer_stack_id = layer_stack_id;

  gfx::Command command;
  command.set_set_layer_stack(std::move(set_layer_stack));
  return command;
}

gfx::Command NewSetRendererCommand(uint32_t layer_id, uint32_t renderer_id) {
  gfx::SetRendererCommand set_renderer;
  set_renderer.layer_id = layer_id;
  set_renderer.renderer_id = renderer_id;

  gfx::Command command;
  command.set_set_renderer(std::move(set_renderer));
  return command;
}

gfx::Command NewSetRendererParamCommand(uint32_t renderer_id,
                                        gfx::RendererParam param) {
  gfx::SetRendererParamCommand param_command;
  param_command.renderer_id = renderer_id;
  param_command.param = std::move(param);

  gfx::Command command;
  command.set_set_renderer_param(std::move(param_command));
  return command;
}

gfx::Command NewSetSizeCommand(uint32_t node_id, const float size[2]) {
  gfx::SetSizeCommand set_size;
  set_size.id = node_id;
  auto& value = set_size.value.value;
  value.x = size[0];
  value.y = size[1];
  set_size.value.variable_id = 0;

  gfx::Command command;
  command.set_set_size(std::move(set_size));

  return command;
}

gfx::Command NewSetCameraTransformCommand(uint32_t camera_id,
                                          const float eye_position[3],
                                          const float eye_look_at[3],
                                          const float eye_up[3]) {
  gfx::SetCameraTransformCommand set_command;
  set_command.camera_id = camera_id;
  set_command.eye_position = NewVector3Value(eye_position);
  set_command.eye_look_at = NewVector3Value(eye_look_at);
  set_command.eye_up = NewVector3Value(eye_up);

  gfx::Command command;
  command.set_set_camera_transform(std::move(set_command));

  return command;
}

gfx::Command NewSetCameraProjectionCommand(uint32_t camera_id,
                                           const float fovy) {
  gfx::SetCameraProjectionCommand set_command;
  set_command.camera_id = camera_id;
  set_command.fovy = NewFloatValue(fovy);

  gfx::Command command;
  command.set_set_camera_projection(std::move(set_command));

  return command;
}

gfx::Command NewSetStereoCameraProjectionCommand(
    uint32_t camera_id,
    const float left_projection[16],
    const float right_projection[16]) {
  gfx::SetStereoCameraProjectionCommand set_command;
  set_command.camera_id = camera_id;
  set_command.left_projection = NewMatrix4Value(left_projection);
  set_command.right_projection = NewMatrix4Value(right_projection);

  gfx::Command command;
  command.set_set_stereo_camera_projection(std::move(set_command));

  return command;
}

gfx::Command NewSetCameraPoseBufferCommand(uint32_t camera_id,
                                           uint32_t buffer_id,
                                           uint32_t num_entries,
                                           uint64_t base_time,
                                           uint64_t time_interval) {
  gfx::SetCameraPoseBufferCommand set_command;
  set_command.camera_id = camera_id;
  set_command.buffer_id = buffer_id;
  set_command.num_entries = num_entries;
  set_command.base_time = base_time;
  set_command.time_interval = time_interval;

  gfx::Command command;
  command.set_set_camera_pose_buffer(std::move(set_command));

  return command;
}

gfx::Command NewSetLightColorCommand(uint32_t light_id, const float rgb[3]) {
  gfx::SetLightColorCommand set_command;
  set_command.light_id = light_id;
  set_command.color = NewColorRgbValue(rgb[0], rgb[1], rgb[2]);

  gfx::Command command;
  command.set_set_light_color(std::move(set_command));

  return command;
}

gfx::Command NewSetLightColorCommand(uint32_t light_id, uint32_t variable_id) {
  gfx::SetLightColorCommand set_command;
  set_command.light_id = light_id;
  set_command.color = NewColorRgbValue(variable_id);

  gfx::Command command;
  command.set_set_light_color(std::move(set_command));

  return command;
}

gfx::Command NewSetLightDirectionCommand(uint32_t light_id,
                                         const float dir[3]) {
  gfx::SetLightDirectionCommand set_command;
  set_command.light_id = light_id;
  set_command.direction = NewVector3Value(dir);

  gfx::Command command;
  command.set_set_light_direction(std::move(set_command));

  return command;
}

gfx::Command NewSetLightDirectionCommand(uint32_t light_id,
                                         uint32_t variable_id) {
  gfx::SetLightDirectionCommand set_command;
  set_command.light_id = light_id;
  set_command.direction = NewVector3Value(variable_id);

  gfx::Command command;
  command.set_set_light_direction(std::move(set_command));

  return command;
}

gfx::Command NewAddLightCommand(uint32_t scene_id, uint32_t light_id) {
  gfx::AddLightCommand add_light_command;
  add_light_command.scene_id = scene_id;
  add_light_command.light_id = light_id;

  gfx::Command command;
  command.set_add_light(std::move(add_light_command));

  return command;
}

gfx::Command NewDetachLightCommand(uint32_t light_id) {
  gfx::DetachLightCommand detach_light_command;
  detach_light_command.light_id = light_id;

  gfx::Command command;
  command.set_detach_light(std::move(detach_light_command));

  return command;
}

gfx::Command NewDetachLightsCommand(uint32_t scene_id) {
  gfx::DetachLightsCommand detach_lights_command;
  detach_lights_command.scene_id = scene_id;

  gfx::Command command;
  command.set_detach_lights(std::move(detach_lights_command));

  return command;
}

gfx::Command NewSetEventMaskCommand(uint32_t resource_id, uint32_t event_mask) {
  gfx::SetEventMaskCommand set_event_mask_command;
  set_event_mask_command.id = resource_id;
  set_event_mask_command.event_mask = event_mask;

  gfx::Command command;
  command.set_set_event_mask(std::move(set_event_mask_command));

  return command;
}

gfx::Command NewSetLabelCommand(uint32_t resource_id,
                                const std::string& label) {
  gfx::SetLabelCommand set_label_command;
  set_label_command.id = resource_id;
  set_label_command.label = label.substr(0, gfx::kLabelMaxLength);

  gfx::Command command;
  command.set_set_label(std::move(set_label_command));

  return command;
}

gfx::Command NewSetDisableClippingCommand(uint32_t renderer_id,
                                          bool disable_clipping) {
  gfx::SetDisableClippingCommand set_disable_clipping_command;
  set_disable_clipping_command.renderer_id = renderer_id;
  set_disable_clipping_command.disable_clipping = disable_clipping;

  gfx::Command command;
  command.set_set_disable_clipping(std::move(set_disable_clipping_command));

  return command;
}

gfx::FloatValue NewFloatValue(float value) {
  gfx::FloatValue val;
  val.variable_id = 0;
  val.value = value;
  return val;
}

gfx::vec2 NewVector2(const float value[2]) {
  gfx::vec2 val;
  val.x = value[0];
  val.y = value[1];
  return val;
}

gfx::Vector2Value NewVector2Value(const float value[2]) {
  gfx::Vector2Value val;
  val.variable_id = 0;
  val.value = NewVector2(value);
  return val;
}

gfx::Vector2Value NewVector2Value(uint32_t variable_id) {
  gfx::Vector2Value val;
  val.variable_id = variable_id;
  return val;
}

gfx::vec3 NewVector3(const float value[3]) {
  gfx::vec3 val;
  val.x = value[0];
  val.y = value[1];
  val.z = value[2];
  return val;
}

gfx::Vector3Value NewVector3Value(const float value[3]) {
  gfx::Vector3Value val;
  val.variable_id = 0;
  val.value = NewVector3(value);
  return val;
}

gfx::Vector3Value NewVector3Value(uint32_t variable_id) {
  gfx::Vector3Value val;
  val.variable_id = variable_id;
  return val;
}

gfx::vec4 NewVector4(const float value[4]) {
  gfx::vec4 val;
  val.x = value[0];
  val.y = value[1];
  val.z = value[2];
  val.w = value[3];
  return val;
}

gfx::Vector4Value NewVector4Value(const float value[4]) {
  gfx::Vector4Value val;
  val.variable_id = 0;
  val.value = NewVector4(value);
  return val;
}

gfx::Vector4Value NewVector4Value(uint32_t variable_id) {
  gfx::Vector4Value val;
  val.variable_id = variable_id;
  return val;
}

gfx::Quaternion NewQuaternion(const float value[4]) {
  gfx::Quaternion val;
  val.x = value[0];
  val.y = value[1];
  val.z = value[2];
  val.w = value[3];
  return val;
}

gfx::QuaternionValue NewQuaternionValue(const float value[4]) {
  gfx::QuaternionValue val;
  val.variable_id = 0;
  val.value = NewQuaternion(value);
  return val;
}

gfx::QuaternionValue NewQuaternionValue(uint32_t variable_id) {
  gfx::QuaternionValue val;
  val.variable_id = variable_id;
  return val;
}

gfx::mat4 NewMatrix4(const float matrix[16]) {
  gfx::mat4 val;
  val.matrix[0] = matrix[0];
  val.matrix[1] = matrix[1];
  val.matrix[2] = matrix[2];
  val.matrix[3] = matrix[3];
  val.matrix[4] = matrix[4];
  val.matrix[5] = matrix[5];
  val.matrix[6] = matrix[6];
  val.matrix[7] = matrix[7];
  val.matrix[8] = matrix[8];
  val.matrix[9] = matrix[9];
  val.matrix[10] = matrix[10];
  val.matrix[11] = matrix[11];
  val.matrix[12] = matrix[12];
  val.matrix[13] = matrix[13];
  val.matrix[14] = matrix[14];
  val.matrix[15] = matrix[15];
  return val;
}

gfx::Matrix4Value NewMatrix4Value(const float matrix[16]) {
  gfx::Matrix4Value val;
  val.variable_id = 0;
  val.value = NewMatrix4(matrix);
  return val;
}

gfx::Matrix4Value NewMatrix4Value(uint32_t variable_id) {
  gfx::Matrix4Value val;
  val.variable_id = variable_id;
  return val;
}

gfx::ColorRgbValue NewColorRgbValue(float red, float green, float blue) {
  gfx::ColorRgbValue val;
  val.variable_id = 0;
  auto& color = val.value;
  color.red = red;
  color.green = green;
  color.blue = blue;

  return val;
}

gfx::ColorRgbValue NewColorRgbValue(uint32_t variable_id) {
  gfx::ColorRgbValue val;
  val.variable_id = variable_id;

  return val;
}

gfx::ColorRgbaValue NewColorRgbaValue(const uint8_t value[4]) {
  gfx::ColorRgbaValue val;
  val.variable_id = 0;
  auto& color = val.value;
  color.red = value[0];
  color.green = value[1];
  color.blue = value[2];
  color.alpha = value[3];

  return val;
}

gfx::ColorRgbaValue NewColorRgbaValue(uint32_t variable_id) {
  gfx::ColorRgbaValue val;
  val.variable_id = variable_id;

  return val;
}

}  // namespace scenic_lib
