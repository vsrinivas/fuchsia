// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/session/session.h"

#include "apps/mozart/src/scene/print_op.h"
#include "apps/mozart/src/scene/renderer/display_renderer.h"
#include "apps/mozart/src/scene/resources/camera.h"
#include "apps/mozart/src/scene/resources/gpu_memory.h"
#include "apps/mozart/src/scene/resources/host_memory.h"
#include "apps/mozart/src/scene/resources/image.h"
#include "apps/mozart/src/scene/resources/image_pipe.h"
#include "apps/mozart/src/scene/resources/image_pipe_handler.h"
#include "apps/mozart/src/scene/resources/lights/directional_light.h"
#include "apps/mozart/src/scene/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene/resources/nodes/node.h"
#include "apps/mozart/src/scene/resources/nodes/scene.h"
#include "apps/mozart/src/scene/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene/resources/nodes/tag_node.h"
#include "apps/mozart/src/scene/resources/shapes/circle_shape.h"
#include "apps/mozart/src/scene/resources/shapes/rectangle_shape.h"
#include "apps/mozart/src/scene/resources/shapes/rounded_rectangle_shape.h"
#include "apps/mozart/src/scene/util/unwrap.h"

#include "escher/shape/mesh.h"
#include "escher/shape/rounded_rect_factory.h"

namespace mozart {
namespace scene {

namespace {

// Makes it convenient to check that a value is constant and of a specific type,
// or a variable.
// TODO: There should also be a convenient way of type-checking a variable;
// this will necessarily involve looking up the value in the ResourceMap.
constexpr std::array<mozart2::Value::Tag, 2> kFloatValueTypes{
    {mozart2::Value::Tag::VECTOR1, mozart2::Value::Tag::VARIABLE_ID}};
constexpr std::array<mozart2::Value::Tag, 2> kVec3ValueTypes{
    {mozart2::Value::Tag::VECTOR3, mozart2::Value::Tag::VARIABLE_ID}};

}  // anonymous namespace

Session::Session(SessionId id,
                 SessionContext* context,
                 ErrorReporter* error_reporter)
    : id_(id),
      context_(context),
      error_reporter_(error_reporter),
      resources_(error_reporter) {
  FTL_DCHECK(context);
  FTL_DCHECK(error_reporter);
}

Session::~Session() {
  FTL_DCHECK(!is_valid_);
}

bool Session::ApplyOp(const mozart2::OpPtr& op) {
  switch (op->which()) {
    case mozart2::Op::Tag::CREATE_RESOURCE:
      return ApplyCreateResourceOp(op->get_create_resource());
    case mozart2::Op::Tag::RELEASE_RESOURCE:
      return ApplyReleaseResourceOp(op->get_release_resource());
    case mozart2::Op::Tag::EXPORT_RESOURCE:
      return ApplyExportResourceOp(op->get_export_resource());
    case mozart2::Op::Tag::IMPORT_RESOURCE:
      return ApplyImportResourceOp(op->get_import_resource());
    case mozart2::Op::Tag::ADD_CHILD:
      return ApplyAddChildOp(op->get_add_child());
    case mozart2::Op::Tag::ADD_PART:
      return ApplyAddPartOp(op->get_add_part());
    case mozart2::Op::Tag::DETACH:
      return ApplyDetachOp(op->get_detach());
    case mozart2::Op::Tag::DETACH_CHILDREN:
      return ApplyDetachChildrenOp(op->get_detach_children());
    case mozart2::Op::Tag::SET_TRANSLATION:
      return ApplySetTranslationOp(op->get_set_translation());
    case mozart2::Op::Tag::SET_SCALE:
      return ApplySetScaleOp(op->get_set_scale());
    case mozart2::Op::Tag::SET_ROTATION:
      return ApplySetRotationOp(op->get_set_rotation());
    case mozart2::Op::Tag::SET_ANCHOR:
      return ApplySetAnchorOp(op->get_set_anchor());
    case mozart2::Op::Tag::SET_SHAPE:
      return ApplySetShapeOp(op->get_set_shape());
    case mozart2::Op::Tag::SET_MATERIAL:
      return ApplySetMaterialOp(op->get_set_material());
    case mozart2::Op::Tag::SET_CLIP:
      return ApplySetClipOp(op->get_set_clip());
    case mozart2::Op::Tag::SET_CAMERA:
      return ApplySetCameraOp(op->get_set_camera());
    case mozart2::Op::Tag::SET_CAMERA_PROJECTION:
      return ApplySetCameraProjectionOp(op->get_set_camera_projection());
    case mozart2::Op::Tag::SET_LIGHT_INTENSITY:
      return ApplySetLightIntensityOp(op->get_set_light_intensity());
    case mozart2::Op::Tag::SET_TEXTURE:
      return ApplySetTextureOp(op->get_set_texture());
    case mozart2::Op::Tag::SET_COLOR:
      return ApplySetColorOp(op->get_set_color());
    case mozart2::Op::Tag::__UNKNOWN__:
      // FIDL validation should make this impossible.
      FTL_CHECK(false);
      return false;
  }
}

bool Session::ApplyCreateResourceOp(const mozart2::CreateResourceOpPtr& op) {
  const ResourceId id = op->id;
  if (id == 0) {
    error_reporter_->ERROR()
        << "scene::Session::ApplyCreateResourceOp(): invalid ID: " << op;
    return false;
  }

  switch (op->resource->which()) {
    case mozart2::Resource::Tag::MEMORY:
      return ApplyCreateMemory(id, op->resource->get_memory());
    case mozart2::Resource::Tag::IMAGE:
      return ApplyCreateImage(id, op->resource->get_image());
    case mozart2::Resource::Tag::IMAGE_PIPE:
      return ApplyCreateImagePipe(id, op->resource->get_image_pipe());
    case mozart2::Resource::Tag::BUFFER:
      return ApplyCreateBuffer(id, op->resource->get_buffer());
    case mozart2::Resource::Tag::SCENE:
      return ApplyCreateScene(id, op->resource->get_scene());
    case mozart2::Resource::Tag::CAMERA:
      return ApplyCreateCamera(id, op->resource->get_camera());
    case mozart2::Resource::Tag::DISPLAY_RENDERER:
      return ApplyCreateDisplayRenderer(id,
                                        op->resource->get_display_renderer());
    case mozart2::Resource::Tag::IMAGE_PIPE_RENDERER:
      return ApplyCreateImagePipeRenderer(
          id, op->resource->get_image_pipe_renderer());
    case mozart2::Resource::Tag::DIRECTIONAL_LIGHT:
      return ApplyCreateDirectionalLight(id,
                                         op->resource->get_directional_light());
    case mozart2::Resource::Tag::RECTANGLE:
      return ApplyCreateRectangle(id, op->resource->get_rectangle());
    case mozart2::Resource::Tag::ROUNDED_RECTANGLE:
      return ApplyCreateRoundedRectangle(id,
                                         op->resource->get_rounded_rectangle());
    case mozart2::Resource::Tag::CIRCLE:
      return ApplyCreateCircle(id, op->resource->get_circle());
    case mozart2::Resource::Tag::MESH:
      return ApplyCreateMesh(id, op->resource->get_mesh());
    case mozart2::Resource::Tag::MATERIAL:
      return ApplyCreateMaterial(id, op->resource->get_material());
    case mozart2::Resource::Tag::CLIP_NODE:
      return ApplyCreateClipNode(id, op->resource->get_clip_node());
    case mozart2::Resource::Tag::ENTITY_NODE:
      return ApplyCreateEntityNode(id, op->resource->get_entity_node());
    case mozart2::Resource::Tag::SHAPE_NODE:
      return ApplyCreateShapeNode(id, op->resource->get_shape_node());
    case mozart2::Resource::Tag::TAG_NODE:
      return ApplyCreateTagNode(id, op->resource->get_tag_node());
    case mozart2::Resource::Tag::VARIABLE:
      return ApplyCreateVariable(id, op->resource->get_variable());
    case mozart2::Resource::Tag::__UNKNOWN__:
      // FIDL validation should make this impossible.
      FTL_CHECK(false);
      return false;
  }
}

bool Session::ApplyReleaseResourceOp(const mozart2::ReleaseResourceOpPtr& op) {
  return resources_.RemoveResource(op->id);
}

bool Session::ApplyExportResourceOp(const mozart2::ExportResourceOpPtr& op) {
  if (auto resource = resources_.FindResource<Resource>(op->id)) {
    return context_->ExportResource(std::move(resource), std::move(op->token));
  }
  return false;
}

bool Session::ApplyImportResourceOp(const mozart2::ImportResourceOpPtr& op) {
  ImportPtr import =
      ftl::MakeRefCounted<Import>(this, op->spec, std::move(op->token));
  context_->ImportResource(import, op->spec, import->import_token());
  return resources_.AddResource(op->id, std::move(import));
}

bool Session::ApplyAddChildOp(const mozart2::AddChildOpPtr& op) {
  // Find the parent and child nodes.
  if (auto parent_node = resources_.FindResource<Node>(op->node_id)) {
    if (auto child_node = resources_.FindResource<Node>(op->child_id)) {
      return parent_node->AddChild(std::move(child_node));
    }
  }
  return false;
}

bool Session::ApplyAddPartOp(const mozart2::AddPartOpPtr& op) {
  // Find the parent and part nodes.
  if (auto parent_node = resources_.FindResource<Node>(op->node_id)) {
    if (auto part_node = resources_.FindResource<Node>(op->part_id)) {
      return parent_node->AddPart(std::move(part_node));
    }
  }
  return false;
}

bool Session::ApplyDetachOp(const mozart2::DetachOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->node_id)) {
    return Node::Detach(node);
  }
  return false;
}

bool Session::ApplyDetachChildrenOp(const mozart2::DetachChildrenOpPtr& op) {
  error_reporter_->ERROR()
      << "scene::Session::ApplyDetachChildrenOp(): unimplemented";
  return false;
}

bool Session::ApplySetTranslationOp(const mozart2::SetTranslationOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->id)) {
    if (IsVariable(op->value)) {
      error_reporter_->ERROR() << "scene::Session::ApplySetTranslationOp(): "
                                  "unimplemented for variable value.";
      return false;
    }
    return node->SetTranslation(UnwrapVector3(op->value));
  }
  return false;
}

bool Session::ApplySetScaleOp(const mozart2::SetScaleOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->id)) {
    if (IsVariable(op->value)) {
      error_reporter_->ERROR() << "scene::Session::ApplySetScaleOp(): "
                                  "unimplemented for variable value.";
      return false;
    }
    return node->SetScale(UnwrapVector3(op->value));
  }
  return false;
}

bool Session::ApplySetRotationOp(const mozart2::SetRotationOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->id)) {
    if (IsVariable(op->value)) {
      error_reporter_->ERROR() << "scene::Session::ApplySetRotationOp(): "
                                  "unimplemented for variable value.";
      return false;
    }
    return node->SetRotation(UnwrapQuaternion(op->value));
  }
  return false;
}

bool Session::ApplySetAnchorOp(const mozart2::SetAnchorOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->id)) {
    if (IsVariable(op->value)) {
      error_reporter_->ERROR() << "scene::Session::ApplySetAnchorOp(): "
                                  "unimplemented for variable value.";
      return false;
    }
    return node->SetAnchor(UnwrapVector3(op->value));
  }
  return false;
}

bool Session::ApplySetShapeOp(const mozart2::SetShapeOpPtr& op) {
  if (auto node = resources_.FindResource<ShapeNode>(op->node_id)) {
    if (auto shape = resources_.FindResource<Shape>(op->shape_id)) {
      node->SetShape(std::move(shape));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetMaterialOp(const mozart2::SetMaterialOpPtr& op) {
  if (auto node = resources_.FindResource<ShapeNode>(op->node_id)) {
    if (auto material = resources_.FindResource<Material>(op->material_id)) {
      node->SetMaterial(std::move(material));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetClipOp(const mozart2::SetClipOpPtr& op) {
  error_reporter_->ERROR() << "scene::Session::ApplySetClipOp(): unimplemented";
  return false;
}

bool Session::ApplySetCameraOp(const mozart2::SetCameraOpPtr& op) {
  if (auto renderer = resources_.FindResource<Renderer>(op->renderer_id)) {
    if (op->camera_id == 0) {
      renderer->SetCamera(nullptr);
      return true;
    } else if (auto camera = resources_.FindResource<Camera>(op->camera_id)) {
      renderer->SetCamera(std::move(camera));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetTextureOp(const mozart2::SetTextureOpPtr& op) {
  if (auto material = resources_.FindResource<Material>(op->material_id)) {
    if (op->texture_id == 0) {
      material->SetTexture(nullptr);
      return true;
    } else if (auto image =
                   resources_.FindResource<ImageBase>(op->texture_id)) {
      material->SetTexture(std::move(image));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetColorOp(const mozart2::SetColorOpPtr& op) {
  if (auto material = resources_.FindResource<Material>(op->material_id)) {
    if (IsVariable(op->color)) {
      error_reporter_->ERROR() << "scene::Session::ApplySetColorOp(): "
                                  "unimplemented for variable color.";
      return false;
    }

    auto& color = op->color->value;
    float red = static_cast<float>(color->red) / 255.f;
    float green = static_cast<float>(color->green) / 255.f;
    float blue = static_cast<float>(color->blue) / 255.f;
    float alpha = static_cast<float>(color->alpha) / 255.f;
    material->SetColor(red, green, blue, alpha);
    return true;
  }
  return false;
}

bool Session::ApplySetCameraProjectionOp(
    const mozart2::SetCameraProjectionOpPtr& op) {
  // TODO(MZ-123): support variables.
  if (IsVariable(op->matrix)) {
    error_reporter_->ERROR() << "scene::Session::ApplySetCameraProjectionOp(): "
                                "unimplemented: variable projection matrix.";
    return false;
  } else if (!IsMatrix4x4(op->matrix)) {
    error_reporter_->ERROR() << "scene::Session::ApplySetCameraProjectionOp(): "
                                "matrix is not a Matrix4x4.";
    return false;
  } else if (auto camera = resources_.FindResource<Camera>(op->camera_id)) {
    camera->SetProjectionMatrix(UnwrapMatrix4x4(op->matrix));
    return true;
  }
  return false;
}

bool Session::ApplySetLightIntensityOp(
    const mozart2::SetLightIntensityOpPtr& op) {
  // TODO(MZ-123): support variables.
  if (IsVariable(op->intensity)) {
    error_reporter_->ERROR() << "scene::Session::ApplySetLightIntensityOp(): "
                                "unimplemented: variable intensity.";
    return false;
  } else if (!IsFloat(op->intensity)) {
    error_reporter_->ERROR() << "scene::Session::ApplySetLightIntensityOp(): "
                                "intensity is not a float.";
    return false;
  } else if (auto light =
                 resources_.FindResource<DirectionalLight>(op->light_id)) {
    light->set_intensity(op->intensity->get_vector1());
    return true;
  }
  return false;
}

bool Session::ApplyCreateMemory(ResourceId id, const mozart2::MemoryPtr& args) {
  auto memory = CreateMemory(id, args);
  return memory ? resources_.AddResource(id, std::move(memory)) : false;
}

bool Session::ApplyCreateImage(ResourceId id, const mozart2::ImagePtr& args) {
  if (auto memory = resources_.FindResource<Memory>(args->memory_id)) {
    if (auto image = CreateImage(id, memory, args)) {
      return resources_.AddResource(id, std::move(image));
    }
  }

  return false;
}

bool Session::ApplyCreateImagePipe(ResourceId id,
                                   const mozart2::ImagePipeArgsPtr& args) {
  auto image_pipe =
      ftl::MakeRefCounted<ImagePipe>(this, std::move(args->image_pipe_request));
  return resources_.AddResource(id, image_pipe);
}

bool Session::ApplyCreateBuffer(ResourceId id, const mozart2::BufferPtr& args) {
  error_reporter_->ERROR()
      << "scene::Session::ApplyCreateBuffer(): unimplemented";
  return false;
}

bool Session::ApplyCreateScene(ResourceId id, const mozart2::ScenePtr& args) {
  auto scene = CreateScene(id, args);
  return scene ? resources_.AddResource(id, std::move(scene)) : false;
}

bool Session::ApplyCreateCamera(ResourceId id, const mozart2::CameraPtr& args) {
  auto camera = CreateCamera(id, args);
  return camera ? resources_.AddResource(id, std::move(camera)) : false;
}

bool Session::ApplyCreateDisplayRenderer(
    ResourceId id,
    const mozart2::DisplayRendererPtr& args) {
  auto renderer = CreateDisplayRenderer(id, args);
  return renderer ? resources_.AddResource(id, std::move(renderer)) : false;
}

bool Session::ApplyCreateImagePipeRenderer(
    ResourceId id,
    const mozart2::ImagePipeRendererPtr& args) {
  auto renderer = CreateImagePipeRenderer(id, args);
  return renderer ? resources_.AddResource(id, std::move(renderer)) : false;
}

bool Session::ApplyCreateDirectionalLight(
    ResourceId id,
    const mozart2::DirectionalLightPtr& args) {
  if (!AssertValueIsOfType(args->direction, kVec3ValueTypes) ||
      !AssertValueIsOfType(args->intensity, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args->direction) || IsVariable(args->intensity)) {
    error_reporter_->ERROR()
        << "scene::Session::ApplyCreateDirectionalLight(): "
           "unimplemented: variable direction/intensity.";
    return false;
  }

  auto light =
      CreateDirectionalLight(id, Unwrap(args->direction->get_vector3()),
                             args->intensity->get_vector1());
  return light ? resources_.AddResource(id, std::move(light)) : false;
}

bool Session::ApplyCreateRectangle(ResourceId id,
                                   const mozart2::RectanglePtr& args) {
  if (!AssertValueIsOfType(args->width, kFloatValueTypes) ||
      !AssertValueIsOfType(args->height, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args->width) || IsVariable(args->height)) {
    error_reporter_->ERROR() << "scene::Session::ApplyCreateRectangle(): "
                                "unimplemented: variable width/height.";
    return false;
  }

  auto rectangle = CreateRectangle(id, args->width->get_vector1(),
                                   args->height->get_vector1());
  return rectangle ? resources_.AddResource(id, std::move(rectangle)) : false;
}

bool Session::ApplyCreateRoundedRectangle(
    ResourceId id,
    const mozart2::RoundedRectanglePtr& args) {
  if (!AssertValueIsOfType(args->width, kFloatValueTypes) ||
      !AssertValueIsOfType(args->height, kFloatValueTypes) ||
      !AssertValueIsOfType(args->top_left_radius, kFloatValueTypes) ||
      !AssertValueIsOfType(args->top_right_radius, kFloatValueTypes) ||
      !AssertValueIsOfType(args->bottom_left_radius, kFloatValueTypes) ||
      !AssertValueIsOfType(args->bottom_right_radius, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args->width) || IsVariable(args->height) ||
      IsVariable(args->top_left_radius) || IsVariable(args->top_right_radius) ||
      IsVariable(args->bottom_left_radius) ||
      IsVariable(args->bottom_right_radius)) {
    error_reporter_->ERROR()
        << "scene::Session::ApplyCreateRoundedRectangle(): "
           "unimplemented: variable width/height/radii.";
    return false;
  }

  auto rectangle = CreateRoundedRectangle(
      id, args->width->get_vector1(), args->height->get_vector1(),
      args->top_left_radius->get_vector1(),
      args->top_right_radius->get_vector1(),
      args->bottom_right_radius->get_vector1(),
      args->bottom_left_radius->get_vector1());
  return rectangle ? resources_.AddResource(id, std::move(rectangle)) : false;
}

bool Session::ApplyCreateCircle(ResourceId id, const mozart2::CirclePtr& args) {
  if (!AssertValueIsOfType(args->radius, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args->radius)) {
    error_reporter_->ERROR() << "scene::Session::ApplyCreateCircle(): "
                                "unimplemented: variable radius.";
    return false;
  }

  auto circle = CreateCircle(id, args->radius->get_vector1());
  return circle ? resources_.AddResource(id, std::move(circle)) : false;
}

bool Session::ApplyCreateMesh(ResourceId id, const mozart2::MeshPtr& args) {
  error_reporter_->ERROR()
      << "scene::Session::ApplyCreateMesh(): unimplemented";
  return false;
}

bool Session::ApplyCreateMaterial(ResourceId id,
                                  const mozart2::MaterialPtr& args) {
  auto material = CreateMaterial(id);
  return material ? resources_.AddResource(id, std::move(material)) : false;
}

bool Session::ApplyCreateClipNode(ResourceId id,
                                  const mozart2::ClipNodePtr& args) {
  auto node = CreateClipNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateEntityNode(ResourceId id,
                                    const mozart2::EntityNodePtr& args) {
  auto node = CreateEntityNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateShapeNode(ResourceId id,
                                   const mozart2::ShapeNodePtr& args) {
  auto node = CreateShapeNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateTagNode(ResourceId id,
                                 const mozart2::TagNodePtr& args) {
  auto node = CreateTagNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateVariable(ResourceId id,
                                  const mozart2::VariablePtr& args) {
  error_reporter_->ERROR()
      << "scene::Session::ApplyCreateVariable(): unimplemented";
  return false;
}

ResourcePtr Session::CreateMemory(ResourceId, const mozart2::MemoryPtr& args) {
  vk::Device device = context()->vk_device();
  switch (args->memory_type) {
    case mozart2::MemoryType::VK_DEVICE_MEMORY:
      return GpuMemory::New(this, device, args, error_reporter_);
    case mozart2::MemoryType::HOST_MEMORY:
      return HostMemory::New(this, device, args, error_reporter_);
  }
}

ResourcePtr Session::CreateImage(ResourceId,
                                 MemoryPtr memory,
                                 const mozart2::ImagePtr& args) {
  return Image::New(this, memory, args, error_reporter_);
}

ResourcePtr Session::CreateScene(ResourceId id, const mozart2::ScenePtr& args) {
  return ftl::MakeRefCounted<Scene>(this, id);
}

ResourcePtr Session::CreateCamera(ResourceId id,
                                  const mozart2::CameraPtr& args) {
  if (auto scene = resources_.FindResource<Scene>(args->scene_id)) {
    return ftl::MakeRefCounted<Camera>(this, id, std::move(scene));
  }
  return ResourcePtr();
}

ResourcePtr Session::CreateDisplayRenderer(
    ResourceId id,
    const mozart2::DisplayRendererPtr& args) {
  return ftl::MakeRefCounted<DisplayRenderer>(
      this, id, context()->frame_scheduler(), context()->escher(),
      context()->GetVulkanSwapchain());
}

ResourcePtr Session::CreateImagePipeRenderer(
    ResourceId id,
    const mozart2::ImagePipeRendererPtr& args) {
  error_reporter_->ERROR() << "scene::Session::CreateImagePipeRenderer(): "
                              "unimplemented.";
  return ResourcePtr();
}

ResourcePtr Session::CreateDirectionalLight(ResourceId id,
                                            escher::vec3 direction,
                                            float intensity) {
  return ftl::MakeRefCounted<DirectionalLight>(this, id, direction, intensity);
}

ResourcePtr Session::CreateClipNode(ResourceId id,
                                    const mozart2::ClipNodePtr& args) {
  error_reporter_->ERROR() << "scene::Session::CreateClipNode(): "
                              "unimplemented.";
  return ResourcePtr();
}

ResourcePtr Session::CreateEntityNode(ResourceId id,
                                      const mozart2::EntityNodePtr& args) {
  return ftl::MakeRefCounted<EntityNode>(this, id);
}

ResourcePtr Session::CreateShapeNode(ResourceId id,
                                     const mozart2::ShapeNodePtr& args) {
  return ftl::MakeRefCounted<ShapeNode>(this, id);
}

ResourcePtr Session::CreateTagNode(ResourceId id,
                                   const mozart2::TagNodePtr& args) {
  return ftl::MakeRefCounted<TagNode>(this, id, args->tag_value);
}

ResourcePtr Session::CreateCircle(ResourceId id, float initial_radius) {
  return ftl::MakeRefCounted<CircleShape>(this, initial_radius);
}

ResourcePtr Session::CreateRectangle(ResourceId id, float width, float height) {
  return ftl::MakeRefCounted<RectangleShape>(this, width, height);
}

ResourcePtr Session::CreateRoundedRectangle(ResourceId id,
                                            float width,
                                            float height,
                                            float top_left_radius,
                                            float top_right_radius,
                                            float bottom_right_radius,
                                            float bottom_left_radius) {
  auto factory = context()->escher_rounded_rect_factory();
  if (!factory) {
    error_reporter_->ERROR() << "scene::Session::CreateRoundedRectangle(): "
                                "no RoundedRectFactory available.";
    return ResourcePtr();
  }

  escher::RoundedRectSpec rect_spec(width, height, top_left_radius,
                                    top_right_radius, bottom_right_radius,
                                    bottom_left_radius);
  escher::MeshSpec mesh_spec{escher::MeshAttribute::kPosition |
                             escher::MeshAttribute::kUV};

  return ftl::MakeRefCounted<RoundedRectangleShape>(
      this, rect_spec, factory->NewRoundedRect(rect_spec, mesh_spec));
}

ResourcePtr Session::CreateMaterial(ResourceId id) {
  return ftl::MakeRefCounted<Material>(this);
}

void Session::TearDown() {
  if (!is_valid_) {
    // TearDown already called.
    return;
  }
  is_valid_ = false;
  resources_.Clear();
  // TODO(MZ-134): Shutting down the session must eagerly collect any
  // exported resources from the resource linker. Currently, the only way
  // to evict an exported entry is to shut down its peer. But this does
  // not handle session shutdown. Fix that bug and turn this log into an
  // assertion.
  if (resource_count_ != 0) {
    error_reporter()->ERROR()
        << "Session::TearDown(): Not all resources have been "
           "collected. See MZ-134.";
  }
  error_reporter_ = nullptr;
}

ErrorReporter* Session::error_reporter() const {
  return error_reporter_ ? error_reporter_ : ErrorReporter::Default();
}

bool Session::AssertValueIsOfType(const mozart2::ValuePtr& value,
                                  const mozart2::Value::Tag* tags,
                                  size_t tag_count) {
  FTL_DCHECK(tag_count > 0);
  for (size_t i = 0; i < tag_count; ++i) {
    if (value->which() == tags[i]) {
      return true;
    }
  }
  std::ostringstream str;
  if (tag_count == 1) {
    str << ", which is not the expected type: " << tags[0] << ".";
  } else {
    str << ", which is not one of the expected types (" << tags[0];
    for (size_t i = 1; i < tag_count; ++i) {
      str << ", " << tags[i];
    }
    str << ").";
  }
  error_reporter_->ERROR() << "scene::Session: received value of type: "
                           << value->which() << str.str();
  return false;
}

void Session::ScheduleUpdate(
    uint64_t presentation_time,
    ::fidl::Array<mozart2::OpPtr> ops,
    ::fidl::Array<mx::event> acquire_fences,
    ::fidl::Array<mx::event> release_events,
    const mozart2::Session::PresentCallback& callback) {
  if (is_valid()) {
    scheduled_updates_.push({presentation_time, std::move(ops),
                             std::move(acquire_fences),
                             std::move(release_events), callback});
    context_->ScheduleSessionUpdate(presentation_time, SessionPtr(this));
  }
}

bool Session::ApplyScheduledUpdates(uint64_t presentation_time,
                                    uint64_t presentation_interval) {
  bool needs_render = false;
  while (!scheduled_updates_.empty() &&
         scheduled_updates_.front().presentation_time <= presentation_time) {
    if (ApplyUpdate(&scheduled_updates_.front())) {
      needs_render = true;
      auto info = mozart2::PresentationInfo::New();
      info->presentation_time = presentation_time;
      info->presentation_interval = presentation_interval;
      scheduled_updates_.front().present_callback(std::move(info));
      scheduled_updates_.pop();

      // TODO: gather statistics about how close the actual
      // presentation_time was to the requested time.
    } else {
      // An error was encountered while applying the update.
      FTL_LOG(WARNING) << "mozart::Session::ApplySessionUpdates() "
                          "initiating teardown.";
      TearDown();
      // Tearing down a session will very probably result in changes to
      // the global scene-graph.
      return true;
    }
  }
  return needs_render;
}

bool Session::ApplyUpdate(Session::Update* update) {
  if (is_valid()) {
    for (auto& op : update->ops) {
      if (!ApplyOp(op)) {
        error_reporter_->ERROR()
            << "scene::Session::ApplyOp() failed to apply Op: " << op;
        return false;
      }
    }
  }
  return true;

  // TODO: acquire_fences and release_fences should be added to a list that is
  // consumed by the FrameScheduler.
}

}  // namespace scene
}  // namespace mozart
