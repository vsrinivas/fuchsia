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
                                mozart2::ImageInfo::PixelFormat format,
                                mozart2::ImageInfo::ColorSpace color_space,
                                mozart2::ImageInfo::Tiling tiling,
                                uint32_t width,
                                uint32_t height,
                                uint32_t stride) {
  auto image = mozart2::Image::New();
  image->memory_id = memory_id;
  image->memory_offset = memory_offset;
  image->info = mozart2::ImageInfo::New();
  image->info->pixel_format = format;
  image->info->color_space = color_space;
  image->info->tiling = tiling;
  image->info->width = width;
  image->info->height = height;
  image->info->stride = stride;

  auto resource = mozart2::Resource::New();
  resource->set_image(std::move(image));

  return NewCreateResourceOp(id, std::move(resource));
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

mozart2::OpPtr NewCreateMaterialOp(uint32_t id,
                                   uint32_t texture_id,
                                   uint8_t red,
                                   uint8_t green,
                                   uint8_t blue,
                                   uint8_t alpha) {
  auto color = mozart2::ColorRgba::New();
  color->red = red;
  color->green = green;
  color->blue = blue;
  color->alpha = alpha;

  auto material = mozart2::Material::New();
  material->texture_id = texture_id;
  material->color = std::move(color);

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

mozart2::OpPtr NewCreateTagNodeOp(uint32_t id, int32_t tag_value) {
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
  auto import_resource = mozart2::ImportResourceOp::New();
  import_resource->id = resource_id;
  import_resource->token = std::move(import_token);
  import_resource->spec = spec;

  auto op = mozart2::Op::New();
  op->set_import_resource(std::move(import_resource));

  return op;
}

mozart2::OpPtr NewBoundExportResourceOp(uint32_t resource_id,
                                        mx::eventpair* out_import_token) {
  mx::eventpair export_token;
  mx_status_t status =
      mx::eventpair::create(0u, &export_token, out_import_token);
  FTL_CHECK(status == MX_OK);
  return NewExportResourceOp(resource_id, std::move(export_token));
}

mozart2::OpPtr NewBoundImportResourceOp(uint32_t resource_id,
                                        mozart2::ImportSpec import_spec,
                                        mx::eventpair* out_export_token) {
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

mozart2::OpPtr NewSetTransformOp(uint32_t node_id,
                                 const float translation[3],
                                 const float scale[3],
                                 const float anchor[3],
                                 const float quaternion[4]) {
  auto set_transform = mozart2::SetTransformOp::New();
  set_transform->node_id = node_id;
  set_transform->transform = mozart2::Transform::New();
  auto& transform = set_transform->transform;
  transform->translation = mozart2::vec3::New();
  transform->translation->x = translation[0];
  transform->translation->y = translation[1];
  transform->translation->z = translation[2];
  transform->scale = mozart2::vec3::New();
  transform->scale->x = scale[0];
  transform->scale->y = scale[1];
  transform->scale->z = scale[2];
  transform->anchor = mozart2::vec3::New();
  transform->anchor->x = anchor[0];
  transform->anchor->y = anchor[1];
  transform->anchor->z = anchor[2];
  transform->rotation = mozart2::Quaternion::New();
  transform->rotation->x = quaternion[0];
  transform->rotation->y = quaternion[1];
  transform->rotation->z = quaternion[2];
  transform->rotation->w = quaternion[3];

  auto op = mozart2::Op::New();
  op->set_set_transform(std::move(set_transform));

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

}  // namespace mozart
