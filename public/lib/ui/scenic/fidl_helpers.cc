// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scenic/fidl_helpers.h"

#include "lib/ftl/logging.h"

namespace scenic_lib {

// Helper function for all resource creation functions.
static scenic::OpPtr NewCreateResourceOp(uint32_t id,
                                         scenic::ResourcePtr resource) {
  auto create_resource = scenic::CreateResourceOp::New();
  create_resource->id = id;
  create_resource->resource = std::move(resource);

  auto op = scenic::Op::New();
  op->set_create_resource(std::move(create_resource));

  return op;
}

scenic::OpPtr NewCreateMemoryOp(uint32_t id,
                                mx::vmo vmo,
                                scenic::MemoryType memory_type) {
  auto memory = scenic::Memory::New();
  memory->vmo = std::move(vmo);
  memory->memory_type = memory_type;

  auto resource = scenic::Resource::New();
  resource->set_memory(std::move(memory));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateImageOp(uint32_t id,
                               uint32_t memory_id,
                               uint32_t memory_offset,
                               scenic::ImageInfoPtr info) {
  auto image = scenic::Image::New();
  image->memory_id = memory_id;
  image->memory_offset = memory_offset;
  image->info = std::move(info);

  auto resource = scenic::Resource::New();
  resource->set_image(std::move(image));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateImagePipeOp(
    uint32_t id,
    ::fidl::InterfaceRequest<scenic::ImagePipe> request) {
  auto image_pipe = scenic::ImagePipeArgs::New();
  image_pipe->image_pipe_request = std::move(request);

  auto resource = scenic::Resource::New();
  resource->set_image_pipe(std::move(image_pipe));
  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateImageOp(uint32_t id,
                               uint32_t memory_id,
                               uint32_t memory_offset,
                               scenic::ImageInfo::PixelFormat format,
                               scenic::ImageInfo::ColorSpace color_space,
                               scenic::ImageInfo::Tiling tiling,
                               uint32_t width,
                               uint32_t height,
                               uint32_t stride) {
  auto info = scenic::ImageInfo::New();
  info->pixel_format = format;
  info->color_space = color_space;
  info->tiling = tiling;
  info->width = width;
  info->height = height;
  info->stride = stride;
  return NewCreateImageOp(id, memory_id, memory_offset, std::move(info));
}

scenic::OpPtr NewCreateBufferOp(uint32_t id,
                                uint32_t memory_id,
                                uint32_t memory_offset,
                                uint32_t num_bytes) {
  auto buffer = scenic::Buffer::New();
  buffer->memory_id = memory_id;
  buffer->memory_offset = memory_offset;
  buffer->num_bytes = num_bytes;

  auto resource = scenic::Resource::New();
  resource->set_buffer(std::move(buffer));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateDisplayCompositorOp(uint32_t id) {
  auto display_compositor = scenic::DisplayCompositor::New();

  auto resource = scenic::Resource::New();
  resource->set_display_compositor(std::move(display_compositor));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateLayerStackOp(uint32_t id) {
  auto layer_stack = scenic::LayerStack::New();

  auto resource = scenic::Resource::New();
  resource->set_layer_stack(std::move(layer_stack));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateLayerOp(uint32_t id) {
  auto layer = scenic::Layer::New();

  auto resource = scenic::Resource::New();
  resource->set_layer(std::move(layer));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateSceneOp(uint32_t id) {
  auto scene = scenic::Scene::New();

  auto resource = scenic::Resource::New();
  resource->set_scene(std::move(scene));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateCameraOp(uint32_t id, uint32_t scene_id) {
  auto camera = scenic::Camera::New();
  camera->scene_id = scene_id;

  auto resource = scenic::Resource::New();
  resource->set_camera(std::move(camera));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateRendererOp(uint32_t id) {
  auto renderer = scenic::Renderer::New();

  auto resource = scenic::Resource::New();
  resource->set_renderer(std::move(renderer));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateCircleOp(uint32_t id, float radius) {
  auto radius_value = scenic::Value::New();
  radius_value->set_vector1(radius);

  auto circle = scenic::Circle::New();
  circle->radius = std::move(radius_value);

  auto resource = scenic::Resource::New();
  resource->set_circle(std::move(circle));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateRectangleOp(uint32_t id, float width, float height) {
  auto width_value = scenic::Value::New();
  width_value->set_vector1(width);

  auto height_value = scenic::Value::New();
  height_value->set_vector1(height);

  auto rectangle = scenic::Rectangle::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);

  auto resource = scenic::Resource::New();
  resource->set_rectangle(std::move(rectangle));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateRoundedRectangleOp(uint32_t id,
                                          float width,
                                          float height,
                                          float top_left_radius,
                                          float top_right_radius,
                                          float bottom_right_radius,
                                          float bottom_left_radius) {
  auto width_value = scenic::Value::New();
  width_value->set_vector1(width);

  auto height_value = scenic::Value::New();
  height_value->set_vector1(height);

  auto top_left_radius_value = scenic::Value::New();
  top_left_radius_value->set_vector1(top_left_radius);

  auto top_right_radius_value = scenic::Value::New();
  top_right_radius_value->set_vector1(top_right_radius);

  auto bottom_right_radius_value = scenic::Value::New();
  bottom_right_radius_value->set_vector1(bottom_right_radius);

  auto bottom_left_radius_value = scenic::Value::New();
  bottom_left_radius_value->set_vector1(bottom_left_radius);

  auto rectangle = scenic::RoundedRectangle::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);
  rectangle->top_left_radius = std::move(top_left_radius_value);
  rectangle->top_right_radius = std::move(top_right_radius_value);
  rectangle->bottom_right_radius = std::move(bottom_right_radius_value);
  rectangle->bottom_left_radius = std::move(bottom_left_radius_value);

  auto resource = scenic::Resource::New();
  resource->set_rounded_rectangle(std::move(rectangle));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateVarCircleOp(uint32_t id,
                                   uint32_t radius_var_id,
                                   uint32_t height_var_id) {
  auto radius_value = scenic::Value::New();
  radius_value->set_variable_id(radius_var_id);

  auto circle = scenic::Circle::New();
  circle->radius = std::move(radius_value);

  auto resource = scenic::Resource::New();
  resource->set_circle(std::move(circle));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateVarRectangleOp(uint32_t id,
                                      uint32_t width_var_id,
                                      uint32_t height_var_id) {
  auto width_value = scenic::Value::New();
  width_value->set_variable_id(width_var_id);

  auto height_value = scenic::Value::New();
  height_value->set_variable_id(height_var_id);

  auto rectangle = scenic::Rectangle::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);

  auto resource = scenic::Resource::New();
  resource->set_rectangle(std::move(rectangle));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateVarRoundedRectangleOp(
    uint32_t id,
    uint32_t width_var_id,
    uint32_t height_var_id,
    uint32_t top_left_radius_var_id,
    uint32_t top_right_radius_var_id,
    uint32_t bottom_left_radius_var_id,
    uint32_t bottom_right_radius_var_id) {
  auto width_value = scenic::Value::New();
  width_value->set_variable_id(width_var_id);

  auto height_value = scenic::Value::New();
  height_value->set_variable_id(height_var_id);

  auto top_left_radius_value = scenic::Value::New();
  top_left_radius_value->set_variable_id(top_left_radius_var_id);

  auto top_right_radius_value = scenic::Value::New();
  top_right_radius_value->set_variable_id(top_right_radius_var_id);

  auto bottom_left_radius_value = scenic::Value::New();
  bottom_left_radius_value->set_variable_id(bottom_left_radius_var_id);

  auto bottom_right_radius_value = scenic::Value::New();
  bottom_right_radius_value->set_variable_id(bottom_right_radius_var_id);

  auto rectangle = scenic::RoundedRectangle::New();
  rectangle->width = std::move(width_value);
  rectangle->height = std::move(height_value);
  rectangle->top_left_radius = std::move(top_left_radius_value);
  rectangle->top_right_radius = std::move(top_right_radius_value);
  rectangle->bottom_left_radius = std::move(bottom_left_radius_value);
  rectangle->bottom_right_radius = std::move(bottom_right_radius_value);

  auto resource = scenic::Resource::New();
  resource->set_rounded_rectangle(std::move(rectangle));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateMeshOp(uint32_t id) {
  auto mesh = scenic::Mesh::New();

  auto resource = scenic::Resource::New();
  resource->set_mesh(std::move(mesh));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateMaterialOp(uint32_t id) {
  auto material = scenic::Material::New();

  auto resource = scenic::Resource::New();
  resource->set_material(std::move(material));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateClipNodeOp(uint32_t id) {
  auto node = scenic::ClipNode::New();

  auto resource = scenic::Resource::New();
  resource->set_clip_node(std::move(node));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateEntityNodeOp(uint32_t id) {
  auto node = scenic::EntityNode::New();

  auto resource = scenic::Resource::New();
  resource->set_entity_node(std::move(node));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateShapeNodeOp(uint32_t id) {
  auto node = scenic::ShapeNode::New();

  auto resource = scenic::Resource::New();
  resource->set_shape_node(std::move(node));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewCreateVariableFloatOp(uint32_t id, float initial_value) {
  auto value = scenic::Value::New();
  value->set_vector1(initial_value);

  auto variable = scenic::Variable::New();
  variable->type = scenic::ValueType::kVector1;
  variable->initial_value = std::move(value);

  auto resource = scenic::Resource::New();
  resource->set_variable(std::move(variable));

  return NewCreateResourceOp(id, std::move(resource));
}

scenic::OpPtr NewReleaseResourceOp(uint32_t id) {
  auto release_resource = scenic::ReleaseResourceOp::New();
  release_resource->id = id;

  auto op = scenic::Op::New();
  op->set_release_resource(std::move(release_resource));

  return op;
}

scenic::OpPtr NewExportResourceOp(uint32_t resource_id,
                                  mx::eventpair export_token) {
  FTL_DCHECK(export_token);

  auto export_resource = scenic::ExportResourceOp::New();
  export_resource->id = resource_id;
  export_resource->token = std::move(export_token);

  auto op = scenic::Op::New();
  op->set_export_resource(std::move(export_resource));

  return op;
}

scenic::OpPtr NewImportResourceOp(uint32_t resource_id,
                                  scenic::ImportSpec spec,
                                  mx::eventpair import_token) {
  FTL_DCHECK(import_token);

  auto import_resource = scenic::ImportResourceOp::New();
  import_resource->id = resource_id;
  import_resource->token = std::move(import_token);
  import_resource->spec = spec;

  auto op = scenic::Op::New();
  op->set_import_resource(std::move(import_resource));

  return op;
}

scenic::OpPtr NewExportResourceOpAsRequest(uint32_t resource_id,
                                           mx::eventpair* out_import_token) {
  FTL_DCHECK(out_import_token);
  FTL_DCHECK(!*out_import_token);

  mx::eventpair export_token;
  mx_status_t status =
      mx::eventpair::create(0u, &export_token, out_import_token);
  FTL_CHECK(status == MX_OK) << "event pair create failed: status=" << status;
  return NewExportResourceOp(resource_id, std::move(export_token));
}

scenic::OpPtr NewImportResourceOpAsRequest(uint32_t resource_id,
                                           scenic::ImportSpec import_spec,
                                           mx::eventpair* out_export_token) {
  FTL_DCHECK(out_export_token);
  FTL_DCHECK(!*out_export_token);

  mx::eventpair import_token;
  mx_status_t status =
      mx::eventpair::create(0u, &import_token, out_export_token);
  FTL_CHECK(status == MX_OK) << "event pair create failed: status=" << status;
  return NewImportResourceOp(resource_id, import_spec, std::move(import_token));
}

scenic::OpPtr NewAddChildOp(uint32_t node_id, uint32_t child_id) {
  auto add_child = scenic::AddChildOp::New();
  add_child->node_id = node_id;
  add_child->child_id = child_id;

  auto op = scenic::Op::New();
  op->set_add_child(std::move(add_child));

  return op;
}

scenic::OpPtr NewAddPartOp(uint32_t node_id, uint32_t part_id) {
  auto add_part = scenic::AddPartOp::New();
  add_part->node_id = node_id;
  add_part->part_id = part_id;

  auto op = scenic::Op::New();
  op->set_add_part(std::move(add_part));

  return op;
}

scenic::OpPtr NewDetachOp(uint32_t id) {
  auto detach = scenic::DetachOp::New();
  detach->id = id;

  auto op = scenic::Op::New();
  op->set_detach(std::move(detach));

  return op;
}

scenic::OpPtr NewDetachChildrenOp(uint32_t node_id) {
  auto detach_children = scenic::DetachChildrenOp::New();
  detach_children->node_id = node_id;

  auto op = scenic::Op::New();
  op->set_detach_children(std::move(detach_children));

  return op;
}

scenic::OpPtr NewSetTranslationOp(uint32_t node_id,
                                  const float translation[3]) {
  auto set_translation = scenic::SetTranslationOp::New();
  set_translation->id = node_id;
  set_translation->value = scenic::Vector3Value::New();
  set_translation->value->value = scenic::vec3::New();
  auto& value = set_translation->value->value;
  value->x = translation[0];
  value->y = translation[1];
  value->z = translation[2];
  set_translation->value->variable_id = 0;

  auto op = scenic::Op::New();
  op->set_set_translation(std::move(set_translation));

  return op;
}

scenic::OpPtr NewSetScaleOp(uint32_t node_id, const float scale[3]) {
  auto set_scale = scenic::SetScaleOp::New();
  set_scale->id = node_id;
  set_scale->value = scenic::Vector3Value::New();
  set_scale->value->value = scenic::vec3::New();
  auto& value = set_scale->value->value;
  value->x = scale[0];
  value->y = scale[1];
  value->z = scale[2];
  set_scale->value->variable_id = 0;

  auto op = scenic::Op::New();
  op->set_set_scale(std::move(set_scale));

  return op;
}

scenic::OpPtr NewSetRotationOp(uint32_t node_id, const float quaternion[4]) {
  auto set_rotation = scenic::SetRotationOp::New();
  set_rotation->id = node_id;
  set_rotation->value = scenic::QuaternionValue::New();
  set_rotation->value->value = scenic::Quaternion::New();
  auto& value = set_rotation->value->value;
  value->x = quaternion[0];
  value->y = quaternion[1];
  value->z = quaternion[2];
  value->w = quaternion[3];
  set_rotation->value->variable_id = 0;

  auto op = scenic::Op::New();
  op->set_set_rotation(std::move(set_rotation));

  return op;
}

scenic::OpPtr NewSetAnchorOp(uint32_t node_id, const float anchor[3]) {
  auto set_anchor = scenic::SetAnchorOp::New();
  set_anchor->id = node_id;
  set_anchor->value = scenic::Vector3Value::New();
  set_anchor->value->value = scenic::vec3::New();
  auto& value = set_anchor->value->value;
  value->x = anchor[0];
  value->y = anchor[1];
  value->z = anchor[2];
  set_anchor->value->variable_id = 0;

  auto op = scenic::Op::New();
  op->set_set_anchor(std::move(set_anchor));

  return op;
}

scenic::OpPtr NewSetShapeOp(uint32_t node_id, uint32_t shape_id) {
  auto set_shape = scenic::SetShapeOp::New();
  set_shape->node_id = node_id;
  set_shape->shape_id = shape_id;

  auto op = scenic::Op::New();
  op->set_set_shape(std::move(set_shape));

  return op;
}

scenic::OpPtr NewSetMaterialOp(uint32_t node_id, uint32_t material_id) {
  auto set_material = scenic::SetMaterialOp::New();
  set_material->node_id = node_id;
  set_material->material_id = material_id;

  auto op = scenic::Op::New();
  op->set_set_material(std::move(set_material));

  return op;
}

scenic::OpPtr NewSetClipOp(uint32_t node_id,
                           uint32_t clip_id,
                           bool clip_to_self) {
  auto set_clip = scenic::SetClipOp::New();
  set_clip->node_id = node_id;
  set_clip->clip_id = clip_id;
  set_clip->clip_to_self = clip_to_self;

  auto op = scenic::Op::New();
  op->set_set_clip(std::move(set_clip));

  return op;
}

scenic::OpPtr NewSetTagOp(uint32_t node_id, uint32_t tag_value) {
  auto set_tag = scenic::SetTagOp::New();
  set_tag->node_id = node_id;
  set_tag->tag_value = tag_value;

  auto op = scenic::Op::New();
  op->set_set_tag(std::move(set_tag));

  return op;
}

scenic::OpPtr NewSetHitTestBehaviorOp(
    uint32_t node_id,
    scenic::HitTestBehavior hit_test_behavior) {
  auto set_hit_test_behavior = scenic::SetHitTestBehaviorOp::New();
  set_hit_test_behavior->node_id = node_id;
  set_hit_test_behavior->hit_test_behavior = hit_test_behavior;

  auto op = scenic::Op::New();
  op->set_set_hit_test_behavior(std::move(set_hit_test_behavior));

  return op;
}

scenic::OpPtr NewSetCameraOp(uint32_t renderer_id, uint32_t camera_id) {
  auto set_camera = scenic::SetCameraOp::New();
  set_camera->renderer_id = renderer_id;
  set_camera->camera_id = camera_id;

  auto op = scenic::Op::New();
  op->set_set_camera(std::move(set_camera));
  return op;
}

scenic::OpPtr NewSetTextureOp(uint32_t material_id, uint32_t texture_id) {
  auto set_texture = scenic::SetTextureOp::New();
  set_texture->material_id = material_id;
  set_texture->texture_id = texture_id;

  auto op = scenic::Op::New();
  op->set_set_texture(std::move(set_texture));
  return op;
}

scenic::OpPtr NewSetColorOp(uint32_t material_id,
                            uint8_t red,
                            uint8_t green,
                            uint8_t blue,
                            uint8_t alpha) {
  auto color = scenic::ColorRgbaValue::New();
  color->value = scenic::ColorRgba::New();
  color->value->red = red;
  color->value->green = green;
  color->value->blue = blue;
  color->value->alpha = alpha;
  color->variable_id = 0;
  auto set_color = scenic::SetColorOp::New();
  set_color->material_id = material_id;
  set_color->color = std::move(color);

  auto op = scenic::Op::New();
  op->set_set_color(std::move(set_color));

  return op;
}

scenic::MeshVertexFormatPtr NewMeshVertexFormat(
    scenic::ValueType position_type,
    scenic::ValueType normal_type,
    scenic::ValueType tex_coord_type) {
  auto vertex_format = scenic::MeshVertexFormat::New();
  vertex_format->position_type = position_type;
  vertex_format->normal_type = normal_type;
  vertex_format->tex_coord_type = tex_coord_type;
  return vertex_format;
}

scenic::OpPtr NewBindMeshBuffersOp(uint32_t mesh_id,
                                   uint32_t index_buffer_id,
                                   scenic::MeshIndexFormat index_format,
                                   uint64_t index_offset,
                                   uint32_t index_count,
                                   uint32_t vertex_buffer_id,
                                   scenic::MeshVertexFormatPtr vertex_format,
                                   uint64_t vertex_offset,
                                   uint32_t vertex_count,
                                   const float bounding_box_min[3],
                                   const float bounding_box_max[3]) {
  auto bind_mesh_buffers = scenic::BindMeshBuffersOp::New();
  bind_mesh_buffers->mesh_id = mesh_id;
  bind_mesh_buffers->index_buffer_id = index_buffer_id;
  bind_mesh_buffers->index_format = index_format;
  bind_mesh_buffers->index_offset = index_offset;
  bind_mesh_buffers->index_count = index_count;
  bind_mesh_buffers->vertex_buffer_id = vertex_buffer_id;
  bind_mesh_buffers->vertex_format = std::move(vertex_format);
  bind_mesh_buffers->vertex_offset = vertex_offset;
  bind_mesh_buffers->vertex_count = vertex_count;
  auto& bbox = bind_mesh_buffers->bounding_box = scenic::BoundingBox::New();
  bbox->min = scenic::vec3::New();
  bbox->min->x = bounding_box_min[0];
  bbox->min->y = bounding_box_min[1];
  bbox->min->z = bounding_box_min[2];
  bbox->max = scenic::vec3::New();
  bbox->max->x = bounding_box_max[0];
  bbox->max->y = bounding_box_max[1];
  bbox->max->z = bounding_box_max[2];

  auto op = scenic::Op::New();
  op->set_bind_mesh_buffers(std::move(bind_mesh_buffers));

  return op;
}

scenic::OpPtr NewAddLayerOp(uint32_t layer_stack_id, uint32_t layer_id) {
  auto add_layer = scenic::AddLayerOp::New();
  add_layer->layer_stack_id = layer_stack_id;
  add_layer->layer_id = layer_id;

  auto op = scenic::Op::New();
  op->set_add_layer(std::move(add_layer));
  return op;
}

scenic::OpPtr NewSetLayerStackOp(uint32_t compositor_id,
                                 uint32_t layer_stack_id) {
  auto set_layer_stack = scenic::SetLayerStackOp::New();
  set_layer_stack->compositor_id = compositor_id;
  set_layer_stack->layer_stack_id = layer_stack_id;

  auto op = scenic::Op::New();
  op->set_set_layer_stack(std::move(set_layer_stack));
  return op;
}

scenic::OpPtr NewSetRendererOp(uint32_t layer_id, uint32_t renderer_id) {
  auto set_renderer = scenic::SetRendererOp::New();
  set_renderer->layer_id = layer_id;
  set_renderer->renderer_id = renderer_id;

  auto op = scenic::Op::New();
  op->set_set_renderer(std::move(set_renderer));
  return op;
}

scenic::OpPtr NewSetSizeOp(uint32_t node_id, const float size[2]) {
  auto set_size = scenic::SetSizeOp::New();
  set_size->id = node_id;
  set_size->value = scenic::Vector2Value::New();
  set_size->value->value = scenic::vec2::New();
  auto& value = set_size->value->value;
  value->x = size[0];
  value->y = size[1];
  set_size->value->variable_id = 0;

  auto op = scenic::Op::New();
  op->set_set_size(std::move(set_size));

  return op;
}

scenic::OpPtr NewSetCameraProjectionOp(uint32_t camera_id,
                                       const float eye_position[3],
                                       const float eye_look_at[3],
                                       const float eye_up[3],
                                       float fovy) {
  auto set_op = scenic::SetCameraProjectionOp::New();
  set_op->camera_id = camera_id;
  set_op->eye_position = NewVector3Value(eye_position);
  set_op->eye_look_at = NewVector3Value(eye_look_at);
  set_op->eye_up = NewVector3Value(eye_up);
  set_op->fovy = NewFloatValue(fovy);

  auto op = scenic::Op::New();
  op->set_set_camera_projection(std::move(set_op));

  return op;
}

scenic::OpPtr NewSetEventMaskOp(uint32_t resource_id, uint32_t event_mask) {
  auto set_event_mask_op = scenic::SetEventMaskOp::New();
  set_event_mask_op->id = resource_id;
  set_event_mask_op->event_mask = event_mask;

  auto op = scenic::Op::New();
  op->set_set_event_mask(std::move(set_event_mask_op));

  return op;
}

scenic::OpPtr NewSetLabelOp(uint32_t resource_id, const std::string& label) {
  auto set_label_op = scenic::SetLabelOp::New();
  set_label_op->id = resource_id;
  set_label_op->label = label.substr(0, scenic::kLabelMaxLength);

  auto op = scenic::Op::New();
  op->set_set_label(std::move(set_label_op));

  return op;
}

scenic::FloatValuePtr NewFloatValue(float value) {
  auto val = scenic::FloatValue::New();
  val->variable_id = 0;
  val->value = value;
  return val;
}

scenic::Vector2ValuePtr NewVector2Value(const float value[2]) {
  auto val = scenic::Vector2Value::New();
  val->variable_id = 0;
  val->value = scenic::vec2::New();
  val->value->x = value[0];
  val->value->y = value[1];
  return val;
}

scenic::Vector3ValuePtr NewVector3Value(const float value[3]) {
  auto val = scenic::Vector3Value::New();
  val->variable_id = 0;
  val->value = scenic::vec3::New();
  val->value->x = value[0];
  val->value->y = value[1];
  val->value->z = value[2];
  return val;
}

scenic::Vector4ValuePtr NewVector4Value(const float value[4]) {
  auto val = scenic::Vector4Value::New();
  val->variable_id = 0;
  val->value = scenic::vec4::New();
  val->value->x = value[0];
  val->value->y = value[1];
  val->value->z = value[2];
  val->value->w = value[3];
  return val;
}

scenic::Matrix4ValuePtr NewMatrix4Value(const float matrix[16]) {
  auto val = scenic::Matrix4Value::New();
  val->variable_id = 0;
  val->value = scenic::mat4::New();
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

}  // namespace scenic_lib
