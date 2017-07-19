// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/engine/session.h"

#include "apps/mozart/src/scene_manager/engine/hit_tester.h"
#include "apps/mozart/src/scene_manager/print_op.h"
#include "apps/mozart/src/scene_manager/resources/camera.h"
#include "apps/mozart/src/scene_manager/resources/gpu_memory.h"
#include "apps/mozart/src/scene_manager/resources/host_memory.h"
#include "apps/mozart/src/scene_manager/resources/image.h"
#include "apps/mozart/src/scene_manager/resources/image_pipe.h"
#include "apps/mozart/src/scene_manager/resources/image_pipe_handler.h"
#include "apps/mozart/src/scene_manager/resources/lights/directional_light.h"
#include "apps/mozart/src/scene_manager/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene_manager/resources/nodes/node.h"
#include "apps/mozart/src/scene_manager/resources/nodes/scene.h"
#include "apps/mozart/src/scene_manager/resources/nodes/shape_node.h"
#include "apps/mozart/src/scene_manager/resources/renderers/display_renderer.h"
#include "apps/mozart/src/scene_manager/resources/shapes/circle_shape.h"
#include "apps/mozart/src/scene_manager/resources/shapes/rectangle_shape.h"
#include "apps/mozart/src/scene_manager/resources/shapes/rounded_rectangle_shape.h"
#include "apps/mozart/src/scene_manager/util/unwrap.h"
#include "apps/mozart/src/scene_manager/util/wrap.h"

#include "escher/renderer/paper_renderer.h"
#include "escher/shape/mesh.h"
#include "escher/shape/rounded_rect_factory.h"

namespace scene_manager {

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

Session::Session(SessionId id, Engine* engine, ErrorReporter* error_reporter)
    : id_(id),
      engine_(engine),
      error_reporter_(error_reporter),
      resources_(error_reporter) {
  FTL_DCHECK(engine);
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
    case mozart2::Op::Tag::SET_TAG:
      return ApplySetTagOp(op->get_set_tag());
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
    case mozart2::Op::Tag::SET_HIT_TEST_BEHAVIOR:
      return ApplySetHitTestBehaviorOp(op->get_set_hit_test_behavior());
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
    case mozart2::Op::Tag::SET_EVENT_MASK:
      return ApplySetEventMaskOp(op->get_set_event_mask());
    case mozart2::Op::Tag::SET_LABEL:
      return ApplySetLabelOp(op->get_set_label());
    case mozart2::Op::Tag::__UNKNOWN__:
      // FIDL validation should make this impossible.
      FTL_CHECK(false);
      return false;
  }
}

bool Session::ApplyCreateResourceOp(const mozart2::CreateResourceOpPtr& op) {
  const mozart::ResourceId id = op->id;
  if (id == 0) {
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplyCreateResourceOp(): invalid ID: "
        << op;
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
    return engine_->ExportResource(std::move(resource), std::move(op->token));
  }
  return false;
}

bool Session::ApplyImportResourceOp(const mozart2::ImportResourceOpPtr& op) {
  ImportPtr import =
      ftl::MakeRefCounted<Import>(this, op->id, op->spec, std::move(op->token));
  engine_->ImportResource(import, op->spec, import->import_token());
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
  if (auto node = resources_.FindResource<Node>(op->node_id)) {
    return node->DetachChildren();
  }
  return false;
}

bool Session::ApplySetTagOp(const mozart2::SetTagOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->node_id)) {
    return node->SetTagValue(op->tag_value);
  }
  return false;
}

bool Session::ApplySetTranslationOp(const mozart2::SetTranslationOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->id)) {
    if (IsVariable(op->value)) {
      error_reporter_->ERROR()
          << "scene_manager::Session::ApplySetTranslationOp(): "
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
      error_reporter_->ERROR() << "scene_manager::Session::ApplySetScaleOp(): "
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
      error_reporter_->ERROR()
          << "scene_manager::Session::ApplySetRotationOp(): "
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
      error_reporter_->ERROR() << "scene_manager::Session::ApplySetAnchorOp(): "
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
  if (op->clip_id != 0) {
    // TODO(MZ-167): Support non-zero clip_id.
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplySetClipOp(): only "
           "clip_to_self is implemented.";
    return false;
  }

  if (auto node = resources_.FindResource<Node>(op->node_id)) {
    return node->SetClipToSelf(op->clip_to_self);
  }

  return false;
}

bool Session::ApplySetHitTestBehaviorOp(
    const mozart2::SetHitTestBehaviorOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->node_id)) {
    return node->SetHitTestBehavior(op->hit_test_behavior);
  }

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
      error_reporter_->ERROR() << "scene_manager::Session::ApplySetColorOp(): "
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

bool Session::ApplySetEventMaskOp(const mozart2::SetEventMaskOpPtr& op) {
  return false;
}

bool Session::ApplySetCameraProjectionOp(
    const mozart2::SetCameraProjectionOpPtr& op) {
  // TODO(MZ-123): support variables.
  if (IsVariable(op->eye_position) || IsVariable(op->eye_look_at) ||
      IsVariable(op->eye_up) || IsVariable(op->fovy)) {
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplySetCameraProjectionOp(): "
           "unimplemented: variable properties.";
    return false;
  } else if (auto camera = resources_.FindResource<Camera>(op->camera_id)) {
    camera->SetProjection(UnwrapVector3(op->eye_position),
                          UnwrapVector3(op->eye_look_at),
                          UnwrapVector3(op->eye_up), UnwrapFloat(op->fovy));
    return true;
  }
  return false;
}

bool Session::ApplySetLightIntensityOp(
    const mozart2::SetLightIntensityOpPtr& op) {
  // TODO(MZ-123): support variables.
  if (IsVariable(op->intensity)) {
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplySetLightIntensityOp(): "
           "unimplemented: variable intensity.";
    return false;
  } else if (!IsFloat(op->intensity)) {
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplySetLightIntensityOp(): "
           "intensity is not a float.";
    return false;
  } else if (auto light =
                 resources_.FindResource<DirectionalLight>(op->light_id)) {
    light->set_intensity(op->intensity->get_vector1());
    return true;
  }
  return false;
}

bool Session::ApplySetLabelOp(const mozart2::SetLabelOpPtr& op) {
  if (auto r = resources_.FindResource<Resource>(op->id)) {
    return r->SetLabel(op->label.get());
  }
  return false;
}

bool Session::ApplyCreateMemory(mozart::ResourceId id,
                                const mozart2::MemoryPtr& args) {
  auto memory = CreateMemory(id, args);
  return memory ? resources_.AddResource(id, std::move(memory)) : false;
}

bool Session::ApplyCreateImage(mozart::ResourceId id,
                               const mozart2::ImagePtr& args) {
  if (auto memory = resources_.FindResource<Memory>(args->memory_id)) {
    if (auto image = CreateImage(id, memory, args)) {
      return resources_.AddResource(id, std::move(image));
    }
  }

  return false;
}

bool Session::ApplyCreateImagePipe(mozart::ResourceId id,
                                   const mozart2::ImagePipeArgsPtr& args) {
  auto image_pipe = ftl::MakeRefCounted<ImagePipe>(
      this, id, std::move(args->image_pipe_request));
  return resources_.AddResource(id, image_pipe);
}

bool Session::ApplyCreateBuffer(mozart::ResourceId id,
                                const mozart2::BufferPtr& args) {
  error_reporter_->ERROR()
      << "scene_manager::Session::ApplyCreateBuffer(): unimplemented";
  return false;
}

bool Session::ApplyCreateScene(mozart::ResourceId id,
                               const mozart2::ScenePtr& args) {
  auto scene = CreateScene(id, args);
  return scene ? resources_.AddResource(id, std::move(scene)) : false;
}

bool Session::ApplyCreateCamera(mozart::ResourceId id,
                                const mozart2::CameraPtr& args) {
  auto camera = CreateCamera(id, args);
  return camera ? resources_.AddResource(id, std::move(camera)) : false;
}

bool Session::ApplyCreateDisplayRenderer(
    mozart::ResourceId id,
    const mozart2::DisplayRendererPtr& args) {
  auto renderer = CreateDisplayRenderer(id, args);
  return renderer ? resources_.AddResource(id, std::move(renderer)) : false;
}

bool Session::ApplyCreateImagePipeRenderer(
    mozart::ResourceId id,
    const mozart2::ImagePipeRendererPtr& args) {
  auto renderer = CreateImagePipeRenderer(id, args);
  return renderer ? resources_.AddResource(id, std::move(renderer)) : false;
}

bool Session::ApplyCreateDirectionalLight(
    mozart::ResourceId id,
    const mozart2::DirectionalLightPtr& args) {
  if (!AssertValueIsOfType(args->direction, kVec3ValueTypes) ||
      !AssertValueIsOfType(args->intensity, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args->direction) || IsVariable(args->intensity)) {
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplyCreateDirectionalLight(): "
           "unimplemented: variable direction/intensity.";
    return false;
  }

  auto light =
      CreateDirectionalLight(id, Unwrap(args->direction->get_vector3()),
                             args->intensity->get_vector1());
  return light ? resources_.AddResource(id, std::move(light)) : false;
}

bool Session::ApplyCreateRectangle(mozart::ResourceId id,
                                   const mozart2::RectanglePtr& args) {
  if (!AssertValueIsOfType(args->width, kFloatValueTypes) ||
      !AssertValueIsOfType(args->height, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args->width) || IsVariable(args->height)) {
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplyCreateRectangle(): "
           "unimplemented: variable width/height.";
    return false;
  }

  auto rectangle = CreateRectangle(id, args->width->get_vector1(),
                                   args->height->get_vector1());
  return rectangle ? resources_.AddResource(id, std::move(rectangle)) : false;
}

bool Session::ApplyCreateRoundedRectangle(
    mozart::ResourceId id,
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
        << "scene_manager::Session::ApplyCreateRoundedRectangle(): "
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

bool Session::ApplyCreateCircle(mozart::ResourceId id,
                                const mozart2::CirclePtr& args) {
  if (!AssertValueIsOfType(args->radius, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args->radius)) {
    error_reporter_->ERROR() << "scene_manager::Session::ApplyCreateCircle(): "
                                "unimplemented: variable radius.";
    return false;
  }

  auto circle = CreateCircle(id, args->radius->get_vector1());
  return circle ? resources_.AddResource(id, std::move(circle)) : false;
}

bool Session::ApplyCreateMesh(mozart::ResourceId id,
                              const mozart2::MeshPtr& args) {
  error_reporter_->ERROR()
      << "scene_manager::Session::ApplyCreateMesh(): unimplemented";
  return false;
}

bool Session::ApplyCreateMaterial(mozart::ResourceId id,
                                  const mozart2::MaterialPtr& args) {
  auto material = CreateMaterial(id);
  return material ? resources_.AddResource(id, std::move(material)) : false;
}

bool Session::ApplyCreateClipNode(mozart::ResourceId id,
                                  const mozart2::ClipNodePtr& args) {
  auto node = CreateClipNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateEntityNode(mozart::ResourceId id,
                                    const mozart2::EntityNodePtr& args) {
  auto node = CreateEntityNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateShapeNode(mozart::ResourceId id,
                                   const mozart2::ShapeNodePtr& args) {
  auto node = CreateShapeNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateVariable(mozart::ResourceId id,
                                  const mozart2::VariablePtr& args) {
  error_reporter_->ERROR()
      << "scene_manager::Session::ApplyCreateVariable(): unimplemented";
  return false;
}

ResourcePtr Session::CreateMemory(mozart::ResourceId id,
                                  const mozart2::MemoryPtr& args) {
  vk::Device device = engine()->vk_device();
  switch (args->memory_type) {
    case mozart2::MemoryType::VK_DEVICE_MEMORY:
      return GpuMemory::New(this, id, device, args, error_reporter_);
    case mozart2::MemoryType::HOST_MEMORY:
      return HostMemory::New(this, id, device, args, error_reporter_);
  }
}

ResourcePtr Session::CreateImage(mozart::ResourceId id,
                                 MemoryPtr memory,
                                 const mozart2::ImagePtr& args) {
  return Image::New(this, id, memory, args->info, args->memory_offset,
                    error_reporter_);
}

ResourcePtr Session::CreateScene(mozart::ResourceId id,
                                 const mozart2::ScenePtr& args) {
  return ftl::MakeRefCounted<Scene>(this, id);
}

ResourcePtr Session::CreateCamera(mozart::ResourceId id,
                                  const mozart2::CameraPtr& args) {
  if (auto scene = resources_.FindResource<Scene>(args->scene_id)) {
    return ftl::MakeRefCounted<Camera>(this, id, std::move(scene));
  }
  return ResourcePtr();
}

ResourcePtr Session::CreateDisplayRenderer(
    mozart::ResourceId id,
    const mozart2::DisplayRendererPtr& args) {
  Display* display = engine()->display_manager()->default_display();
  if (!display) {
    error_reporter_->ERROR() << "There is no default display available.";
    return nullptr;
  }

  if (display->is_claimed()) {
    error_reporter_->ERROR()
        << "The default display has already been claimed by another renderer.";
    return nullptr;
  }
  return ftl::MakeRefCounted<DisplayRenderer>(this, id, display,
                                              engine()->GetVulkanSwapchain());
}

ResourcePtr Session::CreateImagePipeRenderer(
    mozart::ResourceId id,
    const mozart2::ImagePipeRendererPtr& args) {
  error_reporter_->ERROR()
      << "scene_manager::Session::CreateImagePipeRenderer(): "
         "unimplemented.";
  return ResourcePtr();
}

ResourcePtr Session::CreateDirectionalLight(mozart::ResourceId id,
                                            escher::vec3 direction,
                                            float intensity) {
  return ftl::MakeRefCounted<DirectionalLight>(this, id, direction, intensity);
}

ResourcePtr Session::CreateClipNode(mozart::ResourceId id,
                                    const mozart2::ClipNodePtr& args) {
  error_reporter_->ERROR() << "scene_manager::Session::CreateClipNode(): "
                              "unimplemented.";
  return ResourcePtr();
}

ResourcePtr Session::CreateEntityNode(mozart::ResourceId id,
                                      const mozart2::EntityNodePtr& args) {
  return ftl::MakeRefCounted<EntityNode>(this, id);
}

ResourcePtr Session::CreateShapeNode(mozart::ResourceId id,
                                     const mozart2::ShapeNodePtr& args) {
  return ftl::MakeRefCounted<ShapeNode>(this, id);
}

ResourcePtr Session::CreateCircle(mozart::ResourceId id, float initial_radius) {
  return ftl::MakeRefCounted<CircleShape>(this, id, initial_radius);
}

ResourcePtr Session::CreateRectangle(mozart::ResourceId id,
                                     float width,
                                     float height) {
  return ftl::MakeRefCounted<RectangleShape>(this, id, width, height);
}

ResourcePtr Session::CreateRoundedRectangle(mozart::ResourceId id,
                                            float width,
                                            float height,
                                            float top_left_radius,
                                            float top_right_radius,
                                            float bottom_right_radius,
                                            float bottom_left_radius) {
  auto factory = engine()->escher_rounded_rect_factory();
  if (!factory) {
    error_reporter_->ERROR()
        << "scene_manager::Session::CreateRoundedRectangle(): "
           "no RoundedRectFactory available.";
    return ResourcePtr();
  }

  escher::RoundedRectSpec rect_spec(width, height, top_left_radius,
                                    top_right_radius, bottom_right_radius,
                                    bottom_left_radius);
  escher::MeshSpec mesh_spec{escher::MeshAttribute::kPosition |
                             escher::MeshAttribute::kUV};

  return ftl::MakeRefCounted<RoundedRectangleShape>(
      this, id, rect_spec, factory->NewRoundedRect(rect_spec, mesh_spec));
}

ResourcePtr Session::CreateMaterial(mozart::ResourceId id) {
  return ftl::MakeRefCounted<Material>(this, id);
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
  error_reporter_->ERROR() << "scene_manager::Session: received value of type: "
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
    auto acquire_fence_set =
        std::make_unique<AcquireFenceSet>(std::move(acquire_fences));
    // TODO: Consider calling ScheduleSessionUpdate immediately if
    // acquire_fence_set is already ready (which is the case if there are zero
    // acquire fences).
    acquire_fence_set->WaitReadyAsync([this, presentation_time] {
      engine_->ScheduleSessionUpdate(presentation_time, SessionPtr(this));
    });

    scheduled_updates_.push(Update{presentation_time, std::move(ops),
                                   std::move(acquire_fence_set),
                                   std::move(release_events), callback});
  }
}

void Session::ScheduleImagePipeUpdate(uint64_t presentation_time,
                                      ImagePipePtr image_pipe) {
  if (is_valid()) {
    scheduled_image_pipe_updates_.push(
        {presentation_time, std::move(image_pipe)});

    engine_->ScheduleSessionUpdate(presentation_time, SessionPtr(this));
  }
}

bool Session::ApplyScheduledUpdates(uint64_t presentation_time,
                                    uint64_t presentation_interval) {
  bool needs_render = false;
  while (!scheduled_updates_.empty() &&
         scheduled_updates_.front().presentation_time <= presentation_time &&
         scheduled_updates_.front().acquire_fences->ready()) {
    if (ApplyUpdate(&scheduled_updates_.front())) {
      needs_render = true;
      auto info = mozart2::PresentationInfo::New();
      info->presentation_time = presentation_time;
      info->presentation_interval = presentation_interval;
      scheduled_updates_.front().present_callback(std::move(info));

      for (auto& fence : fences_to_release_on_next_update_) {
        engine()->release_fence_signaller()->AddCPUReleaseFence(
            std::move(fence));
      }
      fences_to_release_on_next_update_ =
          std::move(scheduled_updates_.front().release_fences);

      scheduled_updates_.pop();

      // TODO: gather statistics about how close the actual
      // presentation_time was to the requested time.
    } else {
      // An error was encountered while applying the update.
      FTL_LOG(WARNING) << "mozart::Session::ApplyScheduledUpdates(): "
                          "An error was encountered while applying the update. "
                          "Initiating teardown.";

      BeginTearDown();

      // Tearing down a session will very probably result in changes to
      // the global scene-graph.
      return true;
    }
  }

  // TODO: Unify with other session updates.
  while (!scheduled_image_pipe_updates_.empty() &&
         scheduled_image_pipe_updates_.front().presentation_time <=
             presentation_time) {
    needs_render = scheduled_image_pipe_updates_.front().image_pipe->Update(
        presentation_time, presentation_interval);
    scheduled_image_pipe_updates_.pop();
  }

  return needs_render;
}

bool Session::ApplyUpdate(Session::Update* update) {
  if (is_valid()) {
    for (auto& op : update->ops) {
      if (!ApplyOp(op)) {
        error_reporter_->ERROR()
            << "scene_manager::Session::ApplyOp() failed to apply Op: " << op;
        return false;
      }
    }
  }
  return true;

  // TODO: acquire_fences and release_fences should be added to a list that is
  // consumed by the FrameScheduler.
}

void Session::HitTest(uint32_t node_id,
                      mozart2::vec3Ptr ray_origin,
                      mozart2::vec3Ptr ray_direction,
                      const mozart2::Session::HitTestCallback& callback) {
  fidl::Array<mozart2::HitPtr> wrapped_hits;
  if (auto node = resources_.FindResource<Node>(node_id)) {
    HitTester hit_tester;
    std::vector<Hit> hits = hit_tester.HitTest(
        node.get(), escher::ray4{escher::vec4(Unwrap(ray_origin), 1.f),
                                 escher::vec4(Unwrap(ray_direction), 0.f)});
    wrapped_hits.resize(hits.size());
    for (size_t i = 0; i < hits.size(); i++) {
      wrapped_hits[i] = mozart2::Hit::New();
      wrapped_hits[i]->tag_value = hits[i].tag_value;
      wrapped_hits[i]->inverse_transform = Wrap(hits[i].inverse_transform);
      wrapped_hits[i]->distance = hits[i].distance;
    }
  } else {
    // TODO(MZ-162): Currently the test fails if the node isn't presented yet.
    // Perhaps we should given clients more control over which state of
    // the scene graph will be consulted for hit testing purposes.
    error_reporter_->WARN()
        << "Cannot perform hit test because node " << node_id
        << " does not exist in the currently presented content.";
  }
  callback(std::move(wrapped_hits));
}

void Session::BeginTearDown() {
  engine()->TearDownSession(id());
  FTL_DCHECK(!is_valid());
}

}  // namespace scene_manager
