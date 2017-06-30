// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scene/session_helpers.h"

#include "lib/ftl/logging.h"

namespace mozart {

// Helper function for all resource creation functions.
static mozart2::OpPtr NewCreateResourceOp(uint32_t id,
                                          mozart2::ResourcePtr resource) {
  auto create_resource = mozart2::CreateResourceOp::New();
  create_resource->id = id;
  create_resource->resource = std::move(resource);

  auto op = mozart2::Op::New();
  op->set_create_resource(std::move(create_resource));

  return op;
}

mozart2::OpPtr NewCreateMemoryOp(uint32_t id,
                                 mx::vmo vmo,
                                 mozart2::MemoryType memory_type) {
  auto memory = mozart2::Memory::New();
  memory->vmo = std::move(vmo);
  memory->memory_type = memory_type;

  auto resource = mozart2::Resource::New();
  resource->set_memory(std::move(memory));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateImageOp(uint32_t id,
                                uint32_t memory_id,
                                uint32_t memory_offset,
                                mozart2::ImageInfoPtr info) {
  auto image = mozart2::Image::New();
  image->memory_id = memory_id;
  image->memory_offset = memory_offset;
  image->info = std::move(info);

  auto resource = mozart2::Resource::New();
  resource->set_image(std::move(image));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateImagePipeOp(
    uint32_t id,
    ::fidl::InterfaceRequest<mozart2::ImagePipe> request) {
  auto image_pipe = mozart2::ImagePipeArgs::New();
  image_pipe->image_pipe_request = std::move(request);

  auto resource = mozart2::Resource::New();
  resource->set_image_pipe(std::move(image_pipe));
  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateImageOp(uint32_t id,
                                uint32_t memory_id,
                                uint32_t memory_offset,
                                mozart2::ImageInfo::PixelFormat format,
                                mozart2::ImageInfo::ColorSpace color_space,
                                mozart2::ImageInfo::Tiling tiling,
                                uint32_t width,
                                uint32_t height,
                                uint32_t stride) {
  auto info = mozart2::ImageInfo::New();
  info->pixel_format = format;
  info->color_space = color_space;
  info->tiling = tiling;
  info->width = width;
  info->height = height;
  info->stride = stride;
  return NewCreateImageOp(id, memory_id, memory_offset, std::move(info));
}

mozart2::OpPtr NewCreateBufferOp(uint32_t id,
                                 uint32_t memory_id,
                                 uint32_t memory_offset,
                                 uint32_t num_bytes) {
  auto buffer = mozart2::Buffer::New();
  buffer->memory_id = memory_id;
  buffer->memory_offset = memory_offset;
  buffer->num_bytes = num_bytes;

  auto resource = mozart2::Resource::New();
  resource->set_buffer(std::move(buffer));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateSceneOp(uint32_t id) {
  auto scene = mozart2::Scene::New();

  auto resource = mozart2::Resource::New();
  resource->set_scene(std::move(scene));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateCameraOp(uint32_t id, uint32_t scene_id) {
  auto camera = mozart2::Camera::New();
  camera->scene_id = scene_id;

  auto resource = mozart2::Resource::New();
  resource->set_camera(std::move(camera));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateDisplayRendererOp(uint32_t id) {
  auto renderer = mozart2::DisplayRenderer::New();

  auto resource = mozart2::Resource::New();
  resource->set_display_renderer(std::move(renderer));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateCircleOp(uint32_t id, float radius) {
  auto radius_value = mozart2::Value::New();
  radius_value->set_vector1(radius);

  auto circle = mozart2::Circle::New();
  circle->radius = std::move(radius_value);

  auto resource = mozart2::Resource::New();
  resource->set_circle(std::move(circle));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateRectangleOp(uint32_t id, float width, float height) {
  auto width_value = mozart2::Value::New();
  width_value->set_vector1(width);

  auto height_value = mozart2::Value::New();
  height_value->set_vector1(height);

  auto rectangle = mozart2::Rectangle::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);

  auto resource = mozart2::Resource::New();
  resource->set_rectangle(std::move(rectangle));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateRoundedRectangleOp(uint32_t id,
                                           float width,
                                           float height,
                                           float top_left_radius,
                                           float top_right_radius,
                                           float bottom_right_radius,
                                           float bottom_left_radius) {
  auto width_value = mozart2::Value::New();
  width_value->set_vector1(width);

  auto height_value = mozart2::Value::New();
  height_value->set_vector1(height);

  auto top_left_radius_value = mozart2::Value::New();
  top_left_radius_value->set_vector1(top_left_radius);

  auto top_right_radius_value = mozart2::Value::New();
  top_right_radius_value->set_vector1(top_right_radius);

  auto bottom_right_radius_value = mozart2::Value::New();
  bottom_right_radius_value->set_vector1(bottom_right_radius);

  auto bottom_left_radius_value = mozart2::Value::New();
  bottom_left_radius_value->set_vector1(bottom_left_radius);

  auto rectangle = mozart2::RoundedRectangle::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);
  rectangle->top_left_radius = std::move(top_left_radius_value);
  rectangle->top_right_radius = std::move(top_right_radius_value);
  rectangle->bottom_right_radius = std::move(bottom_right_radius_value);
  rectangle->bottom_left_radius = std::move(bottom_left_radius_value);

  auto resource = mozart2::Resource::New();
  resource->set_rounded_rectangle(std::move(rectangle));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateVarCircleOp(uint32_t id,
                                    uint32_t radius_var_id,
                                    uint32_t height_var_id) {
  auto radius_value = mozart2::Value::New();
  radius_value->set_variable_id(radius_var_id);

  auto circle = mozart2::Circle::New();
  circle->radius = std::move(radius_value);

  auto resource = mozart2::Resource::New();
  resource->set_circle(std::move(circle));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateVarRectangleOp(uint32_t id,
                                       uint32_t width_var_id,
                                       uint32_t height_var_id) {
  auto width_value = mozart2::Value::New();
  width_value->set_variable_id(width_var_id);

  auto height_value = mozart2::Value::New();
  height_value->set_variable_id(height_var_id);

  auto rectangle = mozart2::Rectangle::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);

  auto resource = mozart2::Resource::New();
  resource->set_rectangle(std::move(rectangle));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateVarRoundedRectangleOp(
    uint32_t id,
    uint32_t width_var_id,
    uint32_t height_var_id,
    uint32_t top_left_radius_var_id,
    uint32_t top_right_radius_var_id,
    uint32_t bottom_left_radius_var_id,
    uint32_t bottom_right_radius_var_id) {
  auto width_value = mozart2::Value::New();
  width_value->set_variable_id(width_var_id);

  auto height_value = mozart2::Value::New();
  height_value->set_variable_id(height_var_id);

  auto top_left_radius_value = mozart2::Value::New();
  top_left_radius_value->set_variable_id(top_left_radius_var_id);

  auto top_right_radius_value = mozart2::Value::New();
  top_right_radius_value->set_variable_id(top_right_radius_var_id);

  auto bottom_left_radius_value = mozart2::Value::New();
  bottom_left_radius_value->set_variable_id(bottom_left_radius_var_id);

  auto bottom_right_radius_value = mozart2::Value::New();
  bottom_right_radius_value->set_variable_id(bottom_right_radius_var_id);

  auto rectangle = mozart2::RoundedRectangle::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);
  rectangle->top_left_radius = std::move(top_left_radius_value);
  rectangle->top_right_radius = std::move(top_right_radius_value);
  rectangle->bottom_left_radius = std::move(bottom_left_radius_value);
  rectangle->bottom_right_radius = std::move(bottom_right_radius_value);

  auto resource = mozart2::Resource::New();
  resource->set_rounded_rectangle(std::move(rectangle));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateMaterialOp(uint32_t id) {
  auto material = mozart2::Material::New();

  auto resource = mozart2::Resource::New();
  resource->set_material(std::move(material));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateClipNodeOp(uint32_t id) {
  auto node = mozart2::ClipNode::New();

  auto resource = mozart2::Resource::New();
  resource->set_clip_node(std::move(node));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateEntityNodeOp(uint32_t id) {
  auto node = mozart2::EntityNode::New();

  auto resource = mozart2::Resource::New();
  resource->set_entity_node(std::move(node));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateShapeNodeOp(uint32_t id) {
  auto node = mozart2::ShapeNode::New();

  auto resource = mozart2::Resource::New();
  resource->set_shape_node(std::move(node));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateTagNodeOp(uint32_t id, uint32_t tag_value) {
  auto node = mozart2::TagNode::New();
  node->tag_value = tag_value;

  auto resource = mozart2::Resource::New();
  resource->set_tag_node(std::move(node));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewCreateVariableFloatOp(uint32_t id, float initial_value) {
  auto value = mozart2::Value::New();
  value->set_vector1(initial_value);

  auto variable = mozart2::Variable::New();
  variable->type = mozart2::ValueType::kVector1;
  variable->initial_value = std::move(value);

  auto resource = mozart2::Resource::New();
  resource->set_variable(std::move(variable));

  return NewCreateResourceOp(id, std::move(resource));
}

mozart2::OpPtr NewReleaseResourceOp(uint32_t id) {
  auto release_resource = mozart2::ReleaseResourceOp::New();
  release_resource->id = id;

  auto op = mozart2::Op::New();
  op->set_release_resource(std::move(release_resource));

  return op;
}

mozart2::OpPtr NewExportResourceOp(uint32_t resource_id,
                                   mx::eventpair export_token) {
  FTL_DCHECK(export_token);

  auto export_resource = mozart2::ExportResourceOp::New();
  export_resource->id = resource_id;
  export_resource->token = std::move(export_token);

  auto op = mozart2::Op::New();
  op->set_export_resource(std::move(export_resource));

  return op;
}

mozart2::OpPtr NewImportResourceOp(uint32_t resource_id,
                                   mozart2::ImportSpec spec,
                                   mx::eventpair import_token) {
  FTL_DCHECK(import_token);

  auto import_resource = mozart2::ImportResourceOp::New();
  import_resource->id = resource_id;
  import_resource->token = std::move(import_token);
  import_resource->spec = spec;

  auto op = mozart2::Op::New();
  op->set_import_resource(std::move(import_resource));

  return op;
}

mozart2::OpPtr NewExportResourceOpAsRequest(uint32_t resource_id,
                                            mx::eventpair* out_import_token) {
  FTL_DCHECK(out_import_token);
  FTL_DCHECK(!*out_import_token);

  mx::eventpair export_token;
  mx_status_t status =
      mx::eventpair::create(0u, &export_token, out_import_token);
  FTL_CHECK(status == MX_OK);
  return NewExportResourceOp(resource_id, std::move(export_token));
}

mozart2::OpPtr NewImportResourceOpAsRequest(uint32_t resource_id,
                                            mozart2::ImportSpec import_spec,
                                            mx::eventpair* out_export_token) {
  FTL_DCHECK(out_export_token);
  FTL_DCHECK(!*out_export_token);

  mx::eventpair import_token;
  mx_status_t status =
      mx::eventpair::create(0u, &import_token, out_export_token);
  FTL_CHECK(status == MX_OK);
  return NewImportResourceOp(resource_id, import_spec, std::move(import_token));
}

mozart2::OpPtr NewAddChildOp(uint32_t node_id, uint32_t child_id) {
  auto add_child = mozart2::AddChildOp::New();
  add_child->node_id = node_id;
  add_child->child_id = child_id;

  auto op = mozart2::Op::New();
  op->set_add_child(std::move(add_child));

  return op;
}

mozart2::OpPtr NewAddPartOp(uint32_t node_id, uint32_t part_id) {
  auto add_part = mozart2::AddPartOp::New();
  add_part->node_id = node_id;
  add_part->part_id = part_id;

  auto op = mozart2::Op::New();
  op->set_add_part(std::move(add_part));

  return op;
}

mozart2::OpPtr NewDetachOp(uint32_t node_id) {
  auto detach = mozart2::DetachOp::New();
  detach->node_id = node_id;

  auto op = mozart2::Op::New();
  op->set_detach(std::move(detach));

  return op;
}

mozart2::OpPtr NewDetachChildrenOp(uint32_t node_id) {
  auto detach_children = mozart2::DetachChildrenOp::New();
  detach_children->node_id = node_id;

  auto op = mozart2::Op::New();
  op->set_detach_children(std::move(detach_children));

  return op;
}

mozart2::OpPtr NewSetTranslationOp(uint32_t node_id,
                                   const float translation[3]) {
  auto set_translation = mozart2::SetTranslationOp::New();
  set_translation->id = node_id;
  set_translation->value = mozart2::Vector3Value::New();
  set_translation->value->value = mozart2::vec3::New();
  auto& value = set_translation->value->value;
  value->x = translation[0];
  value->y = translation[1];
  value->z = translation[2];
  set_translation->value->variable_id = 0;

  auto op = mozart2::Op::New();
  op->set_set_translation(std::move(set_translation));

  return op;
}

mozart2::OpPtr NewSetScaleOp(uint32_t node_id, const float scale[3]) {
  auto set_scale = mozart2::SetScaleOp::New();
  set_scale->id = node_id;
  set_scale->value = mozart2::Vector3Value::New();
  set_scale->value->value = mozart2::vec3::New();
  auto& value = set_scale->value->value;
  value->x = scale[0];
  value->y = scale[1];
  value->z = scale[2];
  set_scale->value->variable_id = 0;

  auto op = mozart2::Op::New();
  op->set_set_scale(std::move(set_scale));

  return op;
}

mozart2::OpPtr NewSetRotationOp(uint32_t node_id, const float quaternion[4]) {
  auto set_rotation = mozart2::SetRotationOp::New();
  set_rotation->id = node_id;
  set_rotation->value = mozart2::QuaternionValue::New();
  set_rotation->value->value = mozart2::Quaternion::New();
  auto& value = set_rotation->value->value;
  value->x = quaternion[0];
  value->y = quaternion[1];
  value->z = quaternion[2];
  value->w = quaternion[3];
  set_rotation->value->variable_id = 0;

  auto op = mozart2::Op::New();
  op->set_set_rotation(std::move(set_rotation));

  return op;
}

mozart2::OpPtr NewSetAnchorOp(uint32_t node_id, const float anchor[3]) {
  auto set_anchor = mozart2::SetAnchorOp::New();
  set_anchor->id = node_id;
  set_anchor->value = mozart2::Vector3Value::New();
  set_anchor->value->value = mozart2::vec3::New();
  auto& value = set_anchor->value->value;
  value->x = anchor[0];
  value->y = anchor[1];
  value->z = anchor[2];
  set_anchor->value->variable_id = 0;

  auto op = mozart2::Op::New();
  op->set_set_anchor(std::move(set_anchor));

  return op;
}

mozart2::OpPtr NewSetShapeOp(uint32_t node_id, uint32_t shape_id) {
  auto set_shape = mozart2::SetShapeOp::New();
  set_shape->node_id = node_id;
  set_shape->shape_id = shape_id;

  auto op = mozart2::Op::New();
  op->set_set_shape(std::move(set_shape));

  return op;
}

mozart2::OpPtr NewSetMaterialOp(uint32_t node_id, uint32_t material_id) {
  auto set_material = mozart2::SetMaterialOp::New();
  set_material->node_id = node_id;
  set_material->material_id = material_id;

  auto op = mozart2::Op::New();
  op->set_set_material(std::move(set_material));

  return op;
}

mozart2::OpPtr NewSetClipOp(uint32_t node_id, uint32_t clip_id) {
  auto set_clip = mozart2::SetClipOp::New();
  set_clip->node_id = node_id;
  set_clip->clip_id = clip_id;

  auto op = mozart2::Op::New();
  op->set_set_clip(std::move(set_clip));

  return op;
}

mozart2::OpPtr NewSetCameraOp(uint32_t renderer_id, uint32_t camera_id) {
  auto set_camera = mozart2::SetCameraOp::New();
  set_camera->renderer_id = renderer_id;
  set_camera->camera_id = camera_id;

  auto op = mozart2::Op::New();
  op->set_set_camera(std::move(set_camera));
  return op;
}

mozart2::OpPtr NewSetTextureOp(uint32_t material_id, uint32_t texture_id) {
  auto set_texture = mozart2::SetTextureOp::New();
  set_texture->material_id = material_id;
  set_texture->texture_id = texture_id;

  auto op = mozart2::Op::New();
  op->set_set_texture(std::move(set_texture));
  return op;
}

mozart2::OpPtr NewSetColorOp(uint32_t material_id,
                             uint8_t red,
                             uint8_t green,
                             uint8_t blue,
                             uint8_t alpha) {
  auto color = mozart2::ColorRgbaValue::New();
  color->value = mozart2::ColorRgba::New();
  color->value->red = red;
  color->value->green = green;
  color->value->blue = blue;
  color->value->alpha = alpha;
  color->variable_id = 0;
  auto set_color = mozart2::SetColorOp::New();
  set_color->material_id = material_id;
  set_color->color = std::move(color);

  auto op = mozart2::Op::New();
  op->set_set_color(std::move(set_color));

  return op;
}

mozart2::OpPtr NewSetCameraProjectionOp(uint32_t camera_id,
                                        const float eye_position[3],
                                        const float eye_look_at[3],
                                        const float eye_up[3],
                                        float fovy) {
  auto set_op = mozart2::SetCameraProjectionOp::New();
  set_op->camera_id = camera_id;
  set_op->eye_position = NewVector3Value(eye_position);
  set_op->eye_look_at = NewVector3Value(eye_look_at);
  set_op->eye_up = NewVector3Value(eye_up);
  set_op->fovy = NewFloatValue(fovy);

  auto op = mozart2::Op::New();
  op->set_set_camera_projection(std::move(set_op));

  return op;
}

mozart2::FloatValuePtr NewFloatValue(float value) {
  auto val = mozart2::FloatValue::New();
  val->variable_id = 0;
  val->value = value;
  return val;
}

mozart2::Vector2ValuePtr NewVector2Value(const float value[2]) {
  auto val = mozart2::Vector2Value::New();
  val->variable_id = 0;
  val->value = mozart2::vec2::New();
  val->value->x = value[0];
  val->value->y = value[1];
  return val;
}

mozart2::Vector3ValuePtr NewVector3Value(const float value[3]) {
  auto val = mozart2::Vector3Value::New();
  val->variable_id = 0;
  val->value = mozart2::vec3::New();
  val->value->x = value[0];
  val->value->y = value[1];
  val->value->z = value[2];
  return val;
}

mozart2::Vector4ValuePtr NewVector4Value(const float value[4]) {
  auto val = mozart2::Vector4Value::New();
  val->variable_id = 0;
  val->value = mozart2::vec4::New();
  val->value->x = value[0];
  val->value->y = value[1];
  val->value->z = value[2];
  val->value->w = value[3];
  return val;
}

mozart2::Matrix4ValuePtr NewMatrix4Value(const float matrix[16]) {
  auto val = mozart2::Matrix4Value::New();
  val->variable_id = 0;
  val->value = mozart2::mat4::New();
  auto& m = val->value->matrix;
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

  return val;
}

/*
mozart2::ColorRgbaValuePtr NewColorRgbaValue(const glm::vec4& value) {
  return foo;
}

mozart2::QuaternionValuePtr NewQuaternionValue(const glm::quat& value) {
  return foo;
}
*/

}  // namespace mozart
