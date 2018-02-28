// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/scenic/fidl_helpers.h"

#include <array>

#include "lib/fxl/logging.h"

namespace scenic_lib {

ui::CommandPtr NewCommand(ui::gfx::CommandPtr command) {
  ui::CommandPtr scenic_command = ui::Command::New();
  scenic_command->set_gfx(std::move(command));
  return scenic_command;
}

// Helper function for all resource creation functions.
static ui::gfx::CommandPtr NewCreateResourceCommand(
    uint32_t id,
    ui::gfx::ResourceArgsPtr resource) {
  auto create_resource = ui::gfx::CreateResourceCommand::New();
  create_resource->id = id;
  create_resource->resource = std::move(resource);

  auto command = ui::gfx::Command::New();
  command->set_create_resource(std::move(create_resource));

  return command;
}

ui::gfx::CommandPtr NewCreateMemoryCommand(uint32_t id,
                                           zx::vmo vmo,
                                           ui::gfx::MemoryType memory_type) {
  auto memory = ui::gfx::MemoryArgs::New();
  memory->vmo = std::move(vmo);
  memory->memory_type = memory_type;

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_memory(std::move(memory));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateImageCommand(uint32_t id,
                                          uint32_t memory_id,
                                          uint32_t memory_offset,
                                          ui::gfx::ImageInfoPtr info) {
  auto image = ui::gfx::ImageArgs::New();
  image->memory_id = memory_id;
  image->memory_offset = memory_offset;
  image->info = std::move(info);

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_image(std::move(image));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateImagePipeCommand(
    uint32_t id,
    ::f1dl::InterfaceRequest<ui::gfx::ImagePipe> request) {
  auto image_pipe = ui::gfx::ImagePipeArgs::New();
  image_pipe->image_pipe_request = std::move(request);

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_image_pipe(std::move(image_pipe));
  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateImageCommand(
    uint32_t id,
    uint32_t memory_id,
    uint32_t memory_offset,
    ui::gfx::ImageInfo::PixelFormat format,
    ui::gfx::ImageInfo::ColorSpace color_space,
    ui::gfx::ImageInfo::Tiling tiling,
    uint32_t width,
    uint32_t height,
    uint32_t stride) {
  auto info = ui::gfx::ImageInfo::New();
  info->pixel_format = format;
  info->color_space = color_space;
  info->tiling = tiling;
  info->width = width;
  info->height = height;
  info->stride = stride;
  return NewCreateImageCommand(id, memory_id, memory_offset, std::move(info));
}

ui::gfx::CommandPtr NewCreateBufferCommand(uint32_t id,
                                           uint32_t memory_id,
                                           uint32_t memory_offset,
                                           uint32_t num_bytes) {
  auto buffer = ui::gfx::BufferArgs::New();
  buffer->memory_id = memory_id;
  buffer->memory_offset = memory_offset;
  buffer->num_bytes = num_bytes;

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_buffer(std::move(buffer));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateDisplayCompositorCommand(uint32_t id) {
  auto display_compositor = ui::gfx::DisplayCompositorArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_display_compositor(std::move(display_compositor));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateLayerStackCommand(uint32_t id) {
  auto layer_stack = ui::gfx::LayerStackArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_layer_stack(std::move(layer_stack));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateLayerCommand(uint32_t id) {
  auto layer = ui::gfx::LayerArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_layer(std::move(layer));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateSceneCommand(uint32_t id) {
  auto scene = ui::gfx::SceneArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_scene(std::move(scene));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateCameraCommand(uint32_t id, uint32_t scene_id) {
  auto camera = ui::gfx::CameraArgs::New();
  camera->scene_id = scene_id;

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_camera(std::move(camera));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateRendererCommand(uint32_t id) {
  auto renderer = ui::gfx::RendererArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_renderer(std::move(renderer));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateAmbientLightCommand(uint32_t id) {
  auto ambient_light = ui::gfx::AmbientLightArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_ambient_light(std::move(ambient_light));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateDirectionalLightCommand(uint32_t id) {
  auto directional_light = ui::gfx::DirectionalLightArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_directional_light(std::move(directional_light));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateCircleCommand(uint32_t id, float radius) {
  auto radius_value = ui::gfx::Value::New();
  radius_value->set_vector1(radius);

  auto circle = ui::gfx::CircleArgs::New();
  circle->radius = std::move(radius_value);

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_circle(std::move(circle));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateRectangleCommand(uint32_t id,
                                              float width,
                                              float height) {
  auto width_value = ui::gfx::Value::New();
  width_value->set_vector1(width);

  auto height_value = ui::gfx::Value::New();
  height_value->set_vector1(height);

  auto rectangle = ui::gfx::RectangleArgs::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_rectangle(std::move(rectangle));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateRoundedRectangleCommand(uint32_t id,
                                                     float width,
                                                     float height,
                                                     float top_left_radius,
                                                     float top_right_radius,
                                                     float bottom_right_radius,
                                                     float bottom_left_radius) {
  auto width_value = ui::gfx::Value::New();
  width_value->set_vector1(width);

  auto height_value = ui::gfx::Value::New();
  height_value->set_vector1(height);

  auto top_left_radius_value = ui::gfx::Value::New();
  top_left_radius_value->set_vector1(top_left_radius);

  auto top_right_radius_value = ui::gfx::Value::New();
  top_right_radius_value->set_vector1(top_right_radius);

  auto bottom_right_radius_value = ui::gfx::Value::New();
  bottom_right_radius_value->set_vector1(bottom_right_radius);

  auto bottom_left_radius_value = ui::gfx::Value::New();
  bottom_left_radius_value->set_vector1(bottom_left_radius);

  auto rectangle = ui::gfx::RoundedRectangleArgs::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);
  rectangle->top_left_radius = std::move(top_left_radius_value);
  rectangle->top_right_radius = std::move(top_right_radius_value);
  rectangle->bottom_right_radius = std::move(bottom_right_radius_value);
  rectangle->bottom_left_radius = std::move(bottom_left_radius_value);

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_rounded_rectangle(std::move(rectangle));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateVarCircleCommand(uint32_t id,
                                              uint32_t radius_var_id,
                                              uint32_t height_var_id) {
  auto radius_value = ui::gfx::Value::New();
  radius_value->set_variable_id(radius_var_id);

  auto circle = ui::gfx::CircleArgs::New();
  circle->radius = std::move(radius_value);

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_circle(std::move(circle));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateVarRectangleCommand(uint32_t id,
                                                 uint32_t width_var_id,
                                                 uint32_t height_var_id) {
  auto width_value = ui::gfx::Value::New();
  width_value->set_variable_id(width_var_id);

  auto height_value = ui::gfx::Value::New();
  height_value->set_variable_id(height_var_id);

  auto rectangle = ui::gfx::RectangleArgs::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_rectangle(std::move(rectangle));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateVarRoundedRectangleCommand(
    uint32_t id,
    uint32_t width_var_id,
    uint32_t height_var_id,
    uint32_t top_left_radius_var_id,
    uint32_t top_right_radius_var_id,
    uint32_t bottom_left_radius_var_id,
    uint32_t bottom_right_radius_var_id) {
  auto width_value = ui::gfx::Value::New();
  width_value->set_variable_id(width_var_id);

  auto height_value = ui::gfx::Value::New();
  height_value->set_variable_id(height_var_id);

  auto top_left_radius_value = ui::gfx::Value::New();
  top_left_radius_value->set_variable_id(top_left_radius_var_id);

  auto top_right_radius_value = ui::gfx::Value::New();
  top_right_radius_value->set_variable_id(top_right_radius_var_id);

  auto bottom_left_radius_value = ui::gfx::Value::New();
  bottom_left_radius_value->set_variable_id(bottom_left_radius_var_id);

  auto bottom_right_radius_value = ui::gfx::Value::New();
  bottom_right_radius_value->set_variable_id(bottom_right_radius_var_id);

  auto rectangle = ui::gfx::RoundedRectangleArgs::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);
  rectangle->top_left_radius = std::move(top_left_radius_value);
  rectangle->top_right_radius = std::move(top_right_radius_value);
  rectangle->bottom_left_radius = std::move(bottom_left_radius_value);
  rectangle->bottom_right_radius = std::move(bottom_right_radius_value);

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_rounded_rectangle(std::move(rectangle));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateMeshCommand(uint32_t id) {
  auto mesh = ui::gfx::MeshArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_mesh(std::move(mesh));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateMaterialCommand(uint32_t id) {
  auto material = ui::gfx::MaterialArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_material(std::move(material));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateClipNodeCommand(uint32_t id) {
  auto node = ui::gfx::ClipNodeArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_clip_node(std::move(node));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateEntityNodeCommand(uint32_t id) {
  auto node = ui::gfx::EntityNodeArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_entity_node(std::move(node));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateShapeNodeCommand(uint32_t id) {
  auto node = ui::gfx::ShapeNodeArgs::New();

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_shape_node(std::move(node));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewCreateVariableCommand(uint32_t id,
                                             ui::gfx::ValuePtr value) {
  auto variable = ui::gfx::VariableArgs::New();
  switch (value->which()) {
    case ui::gfx::Value::Tag::VECTOR1:
      variable->type = ui::gfx::ValueType::kVector1;
      break;
    case ui::gfx::Value::Tag::VECTOR2:
      variable->type = ui::gfx::ValueType::kVector2;
      break;
    case ui::gfx::Value::Tag::VECTOR3:
      variable->type = ui::gfx::ValueType::kVector3;
      break;
    case ui::gfx::Value::Tag::VECTOR4:
      variable->type = ui::gfx::ValueType::kVector4;
      break;
    case ui::gfx::Value::Tag::MATRIX4X4:
      variable->type = ui::gfx::ValueType::kMatrix4;
      break;
    case ui::gfx::Value::Tag::COLOR_RGB:
      variable->type = ui::gfx::ValueType::kColorRgb;
      break;
    case ui::gfx::Value::Tag::COLOR_RGBA:
      variable->type = ui::gfx::ValueType::kColorRgba;
      break;
    case ui::gfx::Value::Tag::QUATERNION:
      variable->type = ui::gfx::ValueType::kQuaternion;
      break;
    case ui::gfx::Value::Tag::TRANSFORM:
      variable->type = ui::gfx::ValueType::kTransform;
      break;
    case ui::gfx::Value::Tag::DEGREES:
      variable->type = ui::gfx::ValueType::kVector1;
      break;
    case ui::gfx::Value::Tag::VARIABLE_ID:
      // A variable's initial value cannot be another variable.
      return nullptr;
    case ui::gfx::Value::Tag::__UNKNOWN__:
      return nullptr;
  }
  variable->initial_value = std::move(value);

  auto resource = ui::gfx::ResourceArgs::New();
  resource->set_variable(std::move(variable));

  return NewCreateResourceCommand(id, std::move(resource));
}

ui::gfx::CommandPtr NewReleaseResourceCommand(uint32_t id) {
  auto release_resource = ui::gfx::ReleaseResourceCommand::New();
  release_resource->id = id;

  auto command = ui::gfx::Command::New();
  command->set_release_resource(std::move(release_resource));

  return command;
}

ui::gfx::CommandPtr NewExportResourceCommand(uint32_t resource_id,
                                             zx::eventpair export_token) {
  FXL_DCHECK(export_token);

  auto export_resource = ui::gfx::ExportResourceCommand::New();
  export_resource->id = resource_id;
  export_resource->token = std::move(export_token);

  auto command = ui::gfx::Command::New();
  command->set_export_resource(std::move(export_resource));

  return command;
}

ui::gfx::CommandPtr NewImportResourceCommand(uint32_t resource_id,
                                             ui::gfx::ImportSpec spec,
                                             zx::eventpair import_token) {
  FXL_DCHECK(import_token);

  auto import_resource = ui::gfx::ImportResourceCommand::New();
  import_resource->id = resource_id;
  import_resource->token = std::move(import_token);
  import_resource->spec = spec;

  auto command = ui::gfx::Command::New();
  command->set_import_resource(std::move(import_resource));

  return command;
}

ui::gfx::CommandPtr NewExportResourceCommandAsRequest(
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

ui::gfx::CommandPtr NewImportResourceCommandAsRequest(
    uint32_t resource_id,
    ui::gfx::ImportSpec import_spec,
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

ui::gfx::CommandPtr NewAddChildCommand(uint32_t node_id, uint32_t child_id) {
  auto add_child = ui::gfx::AddChildCommand::New();
  add_child->node_id = node_id;
  add_child->child_id = child_id;

  auto command = ui::gfx::Command::New();
  command->set_add_child(std::move(add_child));

  return command;
}

ui::gfx::CommandPtr NewAddPartCommand(uint32_t node_id, uint32_t part_id) {
  auto add_part = ui::gfx::AddPartCommand::New();
  add_part->node_id = node_id;
  add_part->part_id = part_id;

  auto command = ui::gfx::Command::New();
  command->set_add_part(std::move(add_part));

  return command;
}

ui::gfx::CommandPtr NewDetachCommand(uint32_t id) {
  auto detach = ui::gfx::DetachCommand::New();
  detach->id = id;

  auto command = ui::gfx::Command::New();
  command->set_detach(std::move(detach));

  return command;
}

ui::gfx::CommandPtr NewDetachChildrenCommand(uint32_t node_id) {
  auto detach_children = ui::gfx::DetachChildrenCommand::New();
  detach_children->node_id = node_id;

  auto command = ui::gfx::Command::New();
  command->set_detach_children(std::move(detach_children));

  return command;
}

ui::gfx::CommandPtr NewSetTranslationCommand(uint32_t node_id,
                                             const float translation[3]) {
  auto set_translation = ui::gfx::SetTranslationCommand::New();
  set_translation->id = node_id;
  set_translation->value = NewVector3Value(translation);

  auto command = ui::gfx::Command::New();
  command->set_set_translation(std::move(set_translation));

  return command;
}

ui::gfx::CommandPtr NewSetTranslationCommand(uint32_t node_id,
                                             uint32_t variable_id) {
  auto set_translation = ui::gfx::SetTranslationCommand::New();
  set_translation->id = node_id;
  set_translation->value = NewVector3Value(variable_id);

  auto command = ui::gfx::Command::New();
  command->set_set_translation(std::move(set_translation));

  return command;
}

ui::gfx::CommandPtr NewSetScaleCommand(uint32_t node_id, const float scale[3]) {
  auto set_scale = ui::gfx::SetScaleCommand::New();
  set_scale->id = node_id;
  set_scale->value = NewVector3Value(scale);

  auto command = ui::gfx::Command::New();
  command->set_set_scale(std::move(set_scale));

  return command;
}

ui::gfx::CommandPtr NewSetScaleCommand(uint32_t node_id, uint32_t variable_id) {
  auto set_scale = ui::gfx::SetScaleCommand::New();
  set_scale->id = node_id;
  set_scale->value = NewVector3Value(variable_id);

  auto command = ui::gfx::Command::New();
  command->set_set_scale(std::move(set_scale));

  return command;
}

ui::gfx::CommandPtr NewSetRotationCommand(uint32_t node_id,
                                          const float quaternion[4]) {
  auto set_rotation = ui::gfx::SetRotationCommand::New();
  set_rotation->id = node_id;
  set_rotation->value = NewQuaternionValue(quaternion);

  auto command = ui::gfx::Command::New();
  command->set_set_rotation(std::move(set_rotation));

  return command;
}

ui::gfx::CommandPtr NewSetRotationCommand(uint32_t node_id,
                                          uint32_t variable_id) {
  auto set_rotation = ui::gfx::SetRotationCommand::New();
  set_rotation->id = node_id;
  set_rotation->value = NewQuaternionValue(variable_id);

  auto command = ui::gfx::Command::New();
  command->set_set_rotation(std::move(set_rotation));

  return command;
}

ui::gfx::CommandPtr NewSetAnchorCommand(uint32_t node_id,
                                        const float anchor[3]) {
  auto set_anchor = ui::gfx::SetAnchorCommand::New();
  set_anchor->id = node_id;
  set_anchor->value = NewVector3Value(anchor);

  auto command = ui::gfx::Command::New();
  command->set_set_anchor(std::move(set_anchor));

  return command;
}

ui::gfx::CommandPtr NewSetAnchorCommand(uint32_t node_id,
                                        uint32_t variable_id) {
  auto set_anchor = ui::gfx::SetAnchorCommand::New();
  set_anchor->id = node_id;
  set_anchor->value = NewVector3Value(variable_id);

  auto command = ui::gfx::Command::New();
  command->set_set_anchor(std::move(set_anchor));

  return command;
}

ui::gfx::CommandPtr NewSetShapeCommand(uint32_t node_id, uint32_t shape_id) {
  auto set_shape = ui::gfx::SetShapeCommand::New();
  set_shape->node_id = node_id;
  set_shape->shape_id = shape_id;

  auto command = ui::gfx::Command::New();
  command->set_set_shape(std::move(set_shape));

  return command;
}

ui::gfx::CommandPtr NewSetMaterialCommand(uint32_t node_id,
                                          uint32_t material_id) {
  auto set_material = ui::gfx::SetMaterialCommand::New();
  set_material->node_id = node_id;
  set_material->material_id = material_id;

  auto command = ui::gfx::Command::New();
  command->set_set_material(std::move(set_material));

  return command;
}

ui::gfx::CommandPtr NewSetClipCommand(uint32_t node_id,
                                      uint32_t clip_id,
                                      bool clip_to_self) {
  auto set_clip = ui::gfx::SetClipCommand::New();
  set_clip->node_id = node_id;
  set_clip->clip_id = clip_id;
  set_clip->clip_to_self = clip_to_self;

  auto command = ui::gfx::Command::New();
  command->set_set_clip(std::move(set_clip));

  return command;
}

ui::gfx::CommandPtr NewSetTagCommand(uint32_t node_id, uint32_t tag_value) {
  auto set_tag = ui::gfx::SetTagCommand::New();
  set_tag->node_id = node_id;
  set_tag->tag_value = tag_value;

  auto command = ui::gfx::Command::New();
  command->set_set_tag(std::move(set_tag));

  return command;
}

ui::gfx::CommandPtr NewSetHitTestBehaviorCommand(
    uint32_t node_id,
    ui::gfx::HitTestBehavior hit_test_behavior) {
  auto set_hit_test_behavior = ui::gfx::SetHitTestBehaviorCommand::New();
  set_hit_test_behavior->node_id = node_id;
  set_hit_test_behavior->hit_test_behavior = hit_test_behavior;

  auto command = ui::gfx::Command::New();
  command->set_set_hit_test_behavior(std::move(set_hit_test_behavior));

  return command;
}

ui::gfx::CommandPtr NewSetCameraCommand(uint32_t renderer_id,
                                        uint32_t camera_id) {
  auto set_camera = ui::gfx::SetCameraCommand::New();
  set_camera->renderer_id = renderer_id;
  set_camera->camera_id = camera_id;

  auto command = ui::gfx::Command::New();
  command->set_set_camera(std::move(set_camera));
  return command;
}

ui::gfx::CommandPtr NewSetTextureCommand(uint32_t material_id,
                                         uint32_t texture_id) {
  auto set_texture = ui::gfx::SetTextureCommand::New();
  set_texture->material_id = material_id;
  set_texture->texture_id = texture_id;

  auto command = ui::gfx::Command::New();
  command->set_set_texture(std::move(set_texture));
  return command;
}

ui::gfx::CommandPtr NewSetColorCommand(uint32_t material_id,
                                       uint8_t red,
                                       uint8_t green,
                                       uint8_t blue,
                                       uint8_t alpha) {
  auto color = ui::gfx::ColorRgbaValue::New();
  color->value = ui::gfx::ColorRgba::New();
  color->value->red = red;
  color->value->green = green;
  color->value->blue = blue;
  color->value->alpha = alpha;
  color->variable_id = 0;
  auto set_color = ui::gfx::SetColorCommand::New();
  set_color->material_id = material_id;
  set_color->color = std::move(color);

  auto command = ui::gfx::Command::New();
  command->set_set_color(std::move(set_color));

  return command;
}

ui::gfx::MeshVertexFormatPtr NewMeshVertexFormat(
    ui::gfx::ValueType position_type,
    ui::gfx::ValueType normal_type,
    ui::gfx::ValueType tex_coord_type) {
  auto vertex_format = ui::gfx::MeshVertexFormat::New();
  vertex_format->position_type = position_type;
  vertex_format->normal_type = normal_type;
  vertex_format->tex_coord_type = tex_coord_type;
  return vertex_format;
}

ui::gfx::CommandPtr NewBindMeshBuffersCommand(
    uint32_t mesh_id,
    uint32_t index_buffer_id,
    ui::gfx::MeshIndexFormat index_format,
    uint64_t index_offset,
    uint32_t index_count,
    uint32_t vertex_buffer_id,
    ui::gfx::MeshVertexFormatPtr vertex_format,
    uint64_t vertex_offset,
    uint32_t vertex_count,
    const float bounding_box_min[3],
    const float bounding_box_max[3]) {
  auto bind_mesh_buffers = ui::gfx::BindMeshBuffersCommand::New();
  bind_mesh_buffers->mesh_id = mesh_id;
  bind_mesh_buffers->index_buffer_id = index_buffer_id;
  bind_mesh_buffers->index_format = index_format;
  bind_mesh_buffers->index_offset = index_offset;
  bind_mesh_buffers->index_count = index_count;
  bind_mesh_buffers->vertex_buffer_id = vertex_buffer_id;
  bind_mesh_buffers->vertex_format = std::move(vertex_format);
  bind_mesh_buffers->vertex_offset = vertex_offset;
  bind_mesh_buffers->vertex_count = vertex_count;
  auto& bbox = bind_mesh_buffers->bounding_box = ui::gfx::BoundingBox::New();
  bbox->min = ui::gfx::vec3::New();
  bbox->min->x = bounding_box_min[0];
  bbox->min->y = bounding_box_min[1];
  bbox->min->z = bounding_box_min[2];
  bbox->max = ui::gfx::vec3::New();
  bbox->max->x = bounding_box_max[0];
  bbox->max->y = bounding_box_max[1];
  bbox->max->z = bounding_box_max[2];

  auto command = ui::gfx::Command::New();
  command->set_bind_mesh_buffers(std::move(bind_mesh_buffers));

  return command;
}

ui::gfx::CommandPtr NewAddLayerCommand(uint32_t layer_stack_id,
                                       uint32_t layer_id) {
  auto add_layer = ui::gfx::AddLayerCommand::New();
  add_layer->layer_stack_id = layer_stack_id;
  add_layer->layer_id = layer_id;

  auto command = ui::gfx::Command::New();
  command->set_add_layer(std::move(add_layer));
  return command;
}

ui::gfx::CommandPtr NewSetLayerStackCommand(uint32_t compositor_id,
                                            uint32_t layer_stack_id) {
  auto set_layer_stack = ui::gfx::SetLayerStackCommand::New();
  set_layer_stack->compositor_id = compositor_id;
  set_layer_stack->layer_stack_id = layer_stack_id;

  auto command = ui::gfx::Command::New();
  command->set_set_layer_stack(std::move(set_layer_stack));
  return command;
}

ui::gfx::CommandPtr NewSetRendererCommand(uint32_t layer_id,
                                          uint32_t renderer_id) {
  auto set_renderer = ui::gfx::SetRendererCommand::New();
  set_renderer->layer_id = layer_id;
  set_renderer->renderer_id = renderer_id;

  auto command = ui::gfx::Command::New();
  command->set_set_renderer(std::move(set_renderer));
  return command;
}

ui::gfx::CommandPtr NewSetRendererParamCommand(
    uint32_t renderer_id,
    ui::gfx::RendererParamPtr param) {
  auto param_command = ui::gfx::SetRendererParamCommand::New();
  param_command->renderer_id = renderer_id;
  param_command->param = std::move(param);

  auto command = ui::gfx::Command::New();
  command->set_set_renderer_param(std::move(param_command));
  return command;
}

ui::gfx::CommandPtr NewSetSizeCommand(uint32_t node_id, const float size[2]) {
  auto set_size = ui::gfx::SetSizeCommand::New();
  set_size->id = node_id;
  set_size->value = ui::gfx::Vector2Value::New();
  set_size->value->value = ui::gfx::vec2::New();
  auto& value = set_size->value->value;
  value->x = size[0];
  value->y = size[1];
  set_size->value->variable_id = 0;

  auto command = ui::gfx::Command::New();
  command->set_set_size(std::move(set_size));

  return command;
}

ui::gfx::CommandPtr NewSetCameraTransformCommand(uint32_t camera_id,
                                                 const float eye_position[3],
                                                 const float eye_look_at[3],
                                                 const float eye_up[3]) {
  auto set_command = ui::gfx::SetCameraTransformCommand::New();
  set_command->camera_id = camera_id;
  set_command->eye_position = NewVector3Value(eye_position);
  set_command->eye_look_at = NewVector3Value(eye_look_at);
  set_command->eye_up = NewVector3Value(eye_up);

  auto command = ui::gfx::Command::New();
  command->set_set_camera_transform(std::move(set_command));

  return command;
}

ui::gfx::CommandPtr NewSetCameraProjectionCommand(uint32_t camera_id,
                                                  const float fovy) {
  auto set_command = ui::gfx::SetCameraProjectionCommand::New();
  set_command->camera_id = camera_id;
  set_command->fovy = NewFloatValue(fovy);

  auto command = ui::gfx::Command::New();
  command->set_set_camera_projection(std::move(set_command));

  return command;
}

ui::gfx::CommandPtr NewSetCameraPoseBufferCommand(uint32_t camera_id,
                                                  uint32_t buffer_id,
                                                  uint32_t num_entries,
                                                  uint64_t base_time,
                                                  uint64_t time_interval) {
  auto set_command = ui::gfx::SetCameraPoseBufferCommand::New();
  set_command->camera_id = camera_id;
  set_command->buffer_id = buffer_id;
  set_command->num_entries = num_entries;
  set_command->base_time = base_time;
  set_command->time_interval = time_interval;

  auto command = ui::gfx::Command::New();
  command->set_set_camera_pose_buffer(std::move(set_command));

  return command;
}

ui::gfx::CommandPtr NewSetLightColorCommand(uint32_t light_id,
                                            const float rgb[3]) {
  auto set_command = ui::gfx::SetLightColorCommand::New();
  set_command->light_id = light_id;
  set_command->color = NewColorRgbValue(rgb[0], rgb[1], rgb[2]);

  auto command = ui::gfx::Command::New();
  command->set_set_light_color(std::move(set_command));

  return command;
}

ui::gfx::CommandPtr NewSetLightColorCommand(uint32_t light_id,
                                            uint32_t variable_id) {
  auto set_command = ui::gfx::SetLightColorCommand::New();
  set_command->light_id = light_id;
  set_command->color = NewColorRgbValue(variable_id);

  auto command = ui::gfx::Command::New();
  command->set_set_light_color(std::move(set_command));

  return command;
}

ui::gfx::CommandPtr NewSetLightDirectionCommand(uint32_t light_id,
                                                const float dir[3]) {
  auto set_command = ui::gfx::SetLightDirectionCommand::New();
  set_command->light_id = light_id;
  set_command->direction = NewVector3Value(dir);

  auto command = ui::gfx::Command::New();
  command->set_set_light_direction(std::move(set_command));

  return command;
}

ui::gfx::CommandPtr NewSetLightDirectionCommand(uint32_t light_id,
                                                uint32_t variable_id) {
  auto set_command = ui::gfx::SetLightDirectionCommand::New();
  set_command->light_id = light_id;
  set_command->direction = NewVector3Value(variable_id);

  auto command = ui::gfx::Command::New();
  command->set_set_light_direction(std::move(set_command));

  return command;
}

ui::gfx::CommandPtr NewAddLightCommand(uint32_t scene_id, uint32_t light_id) {
  auto add_light_command = ui::gfx::AddLightCommand::New();
  add_light_command->scene_id = scene_id;
  add_light_command->light_id = light_id;

  auto command = ui::gfx::Command::New();
  command->set_add_light(std::move(add_light_command));

  return command;
}

ui::gfx::CommandPtr NewDetachLightCommand(uint32_t light_id) {
  auto detach_light_command = ui::gfx::DetachLightCommand::New();
  detach_light_command->light_id = light_id;

  auto command = ui::gfx::Command::New();
  command->set_detach_light(std::move(detach_light_command));

  return command;
}

ui::gfx::CommandPtr NewDetachLightsCommand(uint32_t scene_id) {
  auto detach_lights_command = ui::gfx::DetachLightsCommand::New();
  detach_lights_command->scene_id = scene_id;

  auto command = ui::gfx::Command::New();
  command->set_detach_lights(std::move(detach_lights_command));

  return command;
}

ui::gfx::CommandPtr NewSetEventMaskCommand(uint32_t resource_id,
                                           uint32_t event_mask) {
  auto set_event_mask_command = ui::gfx::SetEventMaskCommand::New();
  set_event_mask_command->id = resource_id;
  set_event_mask_command->event_mask = event_mask;

  auto command = ui::gfx::Command::New();
  command->set_set_event_mask(std::move(set_event_mask_command));

  return command;
}

ui::gfx::CommandPtr NewSetLabelCommand(uint32_t resource_id,
                                       const std::string& label) {
  auto set_label_command = ui::gfx::SetLabelCommand::New();
  set_label_command->id = resource_id;
  set_label_command->label = label.substr(0, ui::gfx::kLabelMaxLength);

  auto command = ui::gfx::Command::New();
  command->set_set_label(std::move(set_label_command));

  return command;
}

ui::gfx::CommandPtr NewSetDisableClippingCommand(uint32_t renderer_id,
                                                 bool disable_clipping) {
  auto set_disable_clipping_command = ui::gfx::SetDisableClippingCommand::New();
  set_disable_clipping_command->renderer_id = renderer_id;
  set_disable_clipping_command->disable_clipping = disable_clipping;

  auto command = ui::gfx::Command::New();
  command->set_set_disable_clipping(std::move(set_disable_clipping_command));

  return command;
}

ui::gfx::FloatValuePtr NewFloatValue(float value) {
  auto val = ui::gfx::FloatValue::New();
  val->variable_id = 0;
  val->value = value;
  return val;
}

ui::gfx::vec2Ptr NewVector2(const float value[2]) {
  ui::gfx::vec2Ptr val = ui::gfx::vec2::New();
  val->x = value[0];
  val->y = value[1];
  return val;
}

ui::gfx::Vector2ValuePtr NewVector2Value(const float value[2]) {
  auto val = ui::gfx::Vector2Value::New();
  val->variable_id = 0;
  val->value = NewVector2(value);
  return val;
}

ui::gfx::Vector2ValuePtr NewVector2Value(uint32_t variable_id) {
  auto val = ui::gfx::Vector2Value::New();
  val->variable_id = variable_id;
  val->value = ui::gfx::vec2::New();
  return val;
}

ui::gfx::vec3Ptr NewVector3(const float value[3]) {
  ui::gfx::vec3Ptr val = ui::gfx::vec3::New();
  val->x = value[0];
  val->y = value[1];
  val->z = value[2];
  return val;
}

ui::gfx::Vector3ValuePtr NewVector3Value(const float value[3]) {
  auto val = ui::gfx::Vector3Value::New();
  val->variable_id = 0;
  val->value = NewVector3(value);
  return val;
}

ui::gfx::Vector3ValuePtr NewVector3Value(uint32_t variable_id) {
  auto val = ui::gfx::Vector3Value::New();
  val->variable_id = variable_id;
  val->value = ui::gfx::vec3::New();
  return val;
}

ui::gfx::vec4Ptr NewVector4(const float value[4]) {
  ui::gfx::vec4Ptr val = ui::gfx::vec4::New();
  val->x = value[0];
  val->y = value[1];
  val->z = value[2];
  val->w = value[3];
  return val;
}

ui::gfx::Vector4ValuePtr NewVector4Value(const float value[4]) {
  auto val = ui::gfx::Vector4Value::New();
  val->variable_id = 0;
  val->value = NewVector4(value);
  return val;
}

ui::gfx::Vector4ValuePtr NewVector4Value(uint32_t variable_id) {
  auto val = ui::gfx::Vector4Value::New();
  val->variable_id = variable_id;
  val->value = ui::gfx::vec4::New();
  return val;
}

ui::gfx::QuaternionPtr NewQuaternion(const float value[4]) {
  ui::gfx::QuaternionPtr val = ui::gfx::Quaternion::New();
  val->x = value[0];
  val->y = value[1];
  val->z = value[2];
  val->w = value[3];
  return val;
}

ui::gfx::QuaternionValuePtr NewQuaternionValue(const float value[4]) {
  auto val = ui::gfx::QuaternionValue::New();
  val->variable_id = 0;
  val->value = NewQuaternion(value);
  return val;
}

ui::gfx::QuaternionValuePtr NewQuaternionValue(uint32_t variable_id) {
  auto val = ui::gfx::QuaternionValue::New();
  val->variable_id = variable_id;
  val->value = nullptr;
  return val;
}

ui::gfx::mat4Ptr NewMatrix4(const float matrix[16]) {
  std::vector<float> m(16);
  m[0] = matrix[0];
  m[1] = matrix[1];
  m[2] = matrix[2];
  m[3] = matrix[3];
  m[4] = matrix[4];
  m[5] = matrix[5];
  m[6] = matrix[6];
  m[7] = matrix[7];
  m[8] = matrix[8];
  m[9] = matrix[9];
  m[10] = matrix[10];
  m[11] = matrix[11];
  m[12] = matrix[12];
  m[13] = matrix[13];
  m[14] = matrix[14];
  m[15] = matrix[15];
  ui::gfx::mat4Ptr val = ui::gfx::mat4::New();
  val->matrix.reset(std::move(m));
  return val;
}

ui::gfx::Matrix4ValuePtr NewMatrix4Value(const float matrix[16]) {
  auto val = ui::gfx::Matrix4Value::New();
  val->variable_id = 0;
  val->value = NewMatrix4(matrix);
  return val;
}

ui::gfx::Matrix4ValuePtr NewMatrix4Value(uint32_t variable_id) {
  auto val = ui::gfx::Matrix4Value::New();
  val->variable_id = variable_id;
  val->value = ui::gfx::mat4::New();
  return val;
}

ui::gfx::ColorRgbValuePtr NewColorRgbValue(float red, float green, float blue) {
  auto val = ui::gfx::ColorRgbValue::New();
  val->variable_id = 0;
  val->value = ui::gfx::ColorRgb::New();
  auto& color = val->value;
  color->red = red;
  color->green = green;
  color->blue = blue;

  return val;
}

ui::gfx::ColorRgbValuePtr NewColorRgbValue(uint32_t variable_id) {
  auto val = ui::gfx::ColorRgbValue::New();
  val->variable_id = variable_id;
  val->value = ui::gfx::ColorRgb::New();

  return val;
}

ui::gfx::ColorRgbaValuePtr NewColorRgbaValue(const uint8_t value[4]) {
  auto val = ui::gfx::ColorRgbaValue::New();
  val->variable_id = 0;
  val->value = ui::gfx::ColorRgba::New();
  auto& color = val->value;
  color->red = value[0];
  color->green = value[1];
  color->blue = value[2];
  color->alpha = value[3];

  return val;
}

ui::gfx::ColorRgbaValuePtr NewColorRgbaValue(uint32_t variable_id) {
  auto val = ui::gfx::ColorRgbaValue::New();
  val->variable_id = variable_id;
  val->value = ui::gfx::ColorRgba::New();

  return val;
}

}  // namespace scenic_lib
