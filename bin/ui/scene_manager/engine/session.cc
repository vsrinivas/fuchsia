// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/engine/session.h"

#include <trace/event.h>

#include "garnet/bin/ui/scene_manager/engine/hit_tester.h"
#include "garnet/bin/ui/scene_manager/engine/session_handler.h"
#include "garnet/bin/ui/scene_manager/resources/buffer.h"
#include "garnet/bin/ui/scene_manager/resources/camera.h"
#include "garnet/bin/ui/scene_manager/resources/compositor/display_compositor.h"
#include "garnet/bin/ui/scene_manager/resources/compositor/layer.h"
#include "garnet/bin/ui/scene_manager/resources/compositor/layer_stack.h"
#include "garnet/bin/ui/scene_manager/resources/gpu_memory.h"
#include "garnet/bin/ui/scene_manager/resources/host_memory.h"
#include "garnet/bin/ui/scene_manager/resources/image.h"
#include "garnet/bin/ui/scene_manager/resources/image_pipe.h"
#include "garnet/bin/ui/scene_manager/resources/image_pipe_handler.h"
#include "garnet/bin/ui/scene_manager/resources/lights/directional_light.h"
#include "garnet/bin/ui/scene_manager/resources/nodes/entity_node.h"
#include "garnet/bin/ui/scene_manager/resources/nodes/node.h"
#include "garnet/bin/ui/scene_manager/resources/nodes/scene.h"
#include "garnet/bin/ui/scene_manager/resources/nodes/shape_node.h"
#include "garnet/bin/ui/scene_manager/resources/renderers/renderer.h"
#include "garnet/bin/ui/scene_manager/resources/shapes/circle_shape.h"
#include "garnet/bin/ui/scene_manager/resources/shapes/mesh_shape.h"
#include "garnet/bin/ui/scene_manager/resources/shapes/rectangle_shape.h"
#include "garnet/bin/ui/scene_manager/resources/shapes/rounded_rectangle_shape.h"
#include "garnet/bin/ui/scene_manager/util/print_op.h"
#include "garnet/bin/ui/scene_manager/util/unwrap.h"
#include "garnet/bin/ui/scene_manager/util/wrap.h"

#include "lib/escher/shape/mesh.h"
#include "lib/escher/shape/rounded_rect_factory.h"

namespace scene_manager {

namespace {

// Makes it convenient to check that a value is constant and of a specific type,
// or a variable.
// TODO: There should also be a convenient way of type-checking a variable;
// this will necessarily involve looking up the value in the ResourceMap.
constexpr std::array<scenic::Value::Tag, 2> kFloatValueTypes{
    {scenic::Value::Tag::VECTOR1, scenic::Value::Tag::VARIABLE_ID}};
constexpr std::array<scenic::Value::Tag, 2> kVec3ValueTypes{
    {scenic::Value::Tag::VECTOR3, scenic::Value::Tag::VARIABLE_ID}};

}  // anonymous namespace

Session::Session(SessionId id,
                 Engine* engine,
                 EventReporter* event_reporter,
                 ErrorReporter* error_reporter)
    : id_(id),
      engine_(engine),
      error_reporter_(error_reporter),
      event_reporter_(event_reporter),
      resources_(error_reporter),
      weak_factory_(this) {
  FXL_DCHECK(engine);
  FXL_DCHECK(error_reporter);
}

Session::~Session() {
  FXL_DCHECK(!is_valid_);
}

bool Session::ApplyOp(const scenic::OpPtr& op) {
  switch (op->which()) {
    case scenic::Op::Tag::CREATE_RESOURCE:
      return ApplyCreateResourceOp(op->get_create_resource());
    case scenic::Op::Tag::RELEASE_RESOURCE:
      return ApplyReleaseResourceOp(op->get_release_resource());
    case scenic::Op::Tag::EXPORT_RESOURCE:
      return ApplyExportResourceOp(op->get_export_resource());
    case scenic::Op::Tag::IMPORT_RESOURCE:
      return ApplyImportResourceOp(op->get_import_resource());
    case scenic::Op::Tag::ADD_CHILD:
      return ApplyAddChildOp(op->get_add_child());
    case scenic::Op::Tag::ADD_PART:
      return ApplyAddPartOp(op->get_add_part());
    case scenic::Op::Tag::DETACH:
      return ApplyDetachOp(op->get_detach());
    case scenic::Op::Tag::DETACH_CHILDREN:
      return ApplyDetachChildrenOp(op->get_detach_children());
    case scenic::Op::Tag::SET_TAG:
      return ApplySetTagOp(op->get_set_tag());
    case scenic::Op::Tag::SET_TRANSLATION:
      return ApplySetTranslationOp(op->get_set_translation());
    case scenic::Op::Tag::SET_SCALE:
      return ApplySetScaleOp(op->get_set_scale());
    case scenic::Op::Tag::SET_ROTATION:
      return ApplySetRotationOp(op->get_set_rotation());
    case scenic::Op::Tag::SET_ANCHOR:
      return ApplySetAnchorOp(op->get_set_anchor());
    case scenic::Op::Tag::SET_SIZE:
      return ApplySetSizeOp(op->get_set_size());
    case scenic::Op::Tag::SET_SHAPE:
      return ApplySetShapeOp(op->get_set_shape());
    case scenic::Op::Tag::SET_MATERIAL:
      return ApplySetMaterialOp(op->get_set_material());
    case scenic::Op::Tag::SET_CLIP:
      return ApplySetClipOp(op->get_set_clip());
    case scenic::Op::Tag::SET_HIT_TEST_BEHAVIOR:
      return ApplySetHitTestBehaviorOp(op->get_set_hit_test_behavior());
    case scenic::Op::Tag::SET_CAMERA:
      return ApplySetCameraOp(op->get_set_camera());
    case scenic::Op::Tag::SET_CAMERA_PROJECTION:
      return ApplySetCameraProjectionOp(op->get_set_camera_projection());
    case scenic::Op::Tag::SET_LIGHT_INTENSITY:
      return ApplySetLightIntensityOp(op->get_set_light_intensity());
    case scenic::Op::Tag::SET_TEXTURE:
      return ApplySetTextureOp(op->get_set_texture());
    case scenic::Op::Tag::SET_COLOR:
      return ApplySetColorOp(op->get_set_color());
    case scenic::Op::Tag::BIND_MESH_BUFFERS:
      return ApplyBindMeshBuffersOp(op->get_bind_mesh_buffers());
    case scenic::Op::Tag::ADD_LAYER:
      return ApplyAddLayerOp(op->get_add_layer());
    case scenic::Op::Tag::SET_LAYER_STACK:
      return ApplySetLayerStackOp(op->get_set_layer_stack());
    case scenic::Op::Tag::SET_RENDERER:
      return ApplySetRendererOp(op->get_set_renderer());
    case scenic::Op::Tag::SET_RENDERER_PARAM:
      return ApplySetRendererParamOp(op->get_set_renderer_param());
    case scenic::Op::Tag::SET_EVENT_MASK:
      return ApplySetEventMaskOp(op->get_set_event_mask());
    case scenic::Op::Tag::SET_LABEL:
      return ApplySetLabelOp(op->get_set_label());
    case scenic::Op::Tag::SET_DISABLE_CLIPPING:
      return ApplySetDisableClippingOp(op->get_set_disable_clipping());
    case scenic::Op::Tag::__UNKNOWN__:
      // FIDL validation should make this impossible.
      FXL_CHECK(false);
      return false;
  }
}

bool Session::ApplyCreateResourceOp(const scenic::CreateResourceOpPtr& op) {
  const scenic::ResourceId id = op->id;
  if (id == 0) {
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplyCreateResourceOp(): invalid ID: "
        << op;
    return false;
  }

  switch (op->resource->which()) {
    case scenic::Resource::Tag::MEMORY:
      return ApplyCreateMemory(id, op->resource->get_memory());
    case scenic::Resource::Tag::IMAGE:
      return ApplyCreateImage(id, op->resource->get_image());
    case scenic::Resource::Tag::IMAGE_PIPE:
      return ApplyCreateImagePipe(id, op->resource->get_image_pipe());
    case scenic::Resource::Tag::BUFFER:
      return ApplyCreateBuffer(id, op->resource->get_buffer());
    case scenic::Resource::Tag::SCENE:
      return ApplyCreateScene(id, op->resource->get_scene());
    case scenic::Resource::Tag::CAMERA:
      return ApplyCreateCamera(id, op->resource->get_camera());
    case scenic::Resource::Tag::RENDERER:
      return ApplyCreateRenderer(id, op->resource->get_renderer());
    case scenic::Resource::Tag::DIRECTIONAL_LIGHT:
      return ApplyCreateDirectionalLight(id,
                                         op->resource->get_directional_light());
    case scenic::Resource::Tag::RECTANGLE:
      return ApplyCreateRectangle(id, op->resource->get_rectangle());
    case scenic::Resource::Tag::ROUNDED_RECTANGLE:
      return ApplyCreateRoundedRectangle(id,
                                         op->resource->get_rounded_rectangle());
    case scenic::Resource::Tag::CIRCLE:
      return ApplyCreateCircle(id, op->resource->get_circle());
    case scenic::Resource::Tag::MESH:
      return ApplyCreateMesh(id, op->resource->get_mesh());
    case scenic::Resource::Tag::MATERIAL:
      return ApplyCreateMaterial(id, op->resource->get_material());
    case scenic::Resource::Tag::CLIP_NODE:
      return ApplyCreateClipNode(id, op->resource->get_clip_node());
    case scenic::Resource::Tag::ENTITY_NODE:
      return ApplyCreateEntityNode(id, op->resource->get_entity_node());
    case scenic::Resource::Tag::SHAPE_NODE:
      return ApplyCreateShapeNode(id, op->resource->get_shape_node());
    case scenic::Resource::Tag::DISPLAY_COMPOSITOR:
      return ApplyCreateDisplayCompositor(
          id, op->resource->get_display_compositor());
    case scenic::Resource::Tag::IMAGE_PIPE_COMPOSITOR:
      return ApplyCreateImagePipeCompositor(
          id, op->resource->get_image_pipe_compositor());
    case scenic::Resource::Tag::LAYER_STACK:
      return ApplyCreateLayerStack(id, op->resource->get_layer_stack());
    case scenic::Resource::Tag::LAYER:
      return ApplyCreateLayer(id, op->resource->get_layer());
    case scenic::Resource::Tag::VARIABLE:
      return ApplyCreateVariable(id, op->resource->get_variable());
    case scenic::Resource::Tag::__UNKNOWN__:
      // FIDL validation should make this impossible.
      FXL_CHECK(false);
      return false;
  }
}

bool Session::ApplyReleaseResourceOp(const scenic::ReleaseResourceOpPtr& op) {
  return resources_.RemoveResource(op->id);
}

bool Session::ApplyExportResourceOp(const scenic::ExportResourceOpPtr& op) {
  if (!op->token) {
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplyExportResourceOp(): "
           "no token provided.";
    return false;
  }
  if (auto resource = resources_.FindResource<Resource>(op->id)) {
    return engine_->resource_linker()->ExportResource(resource.get(),
                                                      std::move(op->token));
  }
  return false;
}

bool Session::ApplyImportResourceOp(const scenic::ImportResourceOpPtr& op) {
  if (!op->token) {
    error_reporter_->ERROR()
        << "scene_manager::Session::ApplyImportResourceOp(): "
           "no token provided.";
    return false;
  }
  ImportPtr import = fxl::MakeRefCounted<Import>(this, op->id, op->spec);
  return engine_->resource_linker()->ImportResource(import.get(), op->spec,
                                                    std::move(op->token)) &&
         resources_.AddResource(op->id, std::move(import));
}

bool Session::ApplyAddChildOp(const scenic::AddChildOpPtr& op) {
  // Find the parent and child nodes.
  if (auto parent_node = resources_.FindResource<Node>(op->node_id)) {
    if (auto child_node = resources_.FindResource<Node>(op->child_id)) {
      return parent_node->AddChild(std::move(child_node));
    }
  }
  return false;
}

bool Session::ApplyAddPartOp(const scenic::AddPartOpPtr& op) {
  // Find the parent and part nodes.
  if (auto parent_node = resources_.FindResource<Node>(op->node_id)) {
    if (auto part_node = resources_.FindResource<Node>(op->part_id)) {
      return parent_node->AddPart(std::move(part_node));
    }
  }
  return false;
}

bool Session::ApplyDetachOp(const scenic::DetachOpPtr& op) {
  if (auto resource = resources_.FindResource<Resource>(op->id)) {
    return resource->Detach();
  }
  return false;
}

bool Session::ApplyDetachChildrenOp(const scenic::DetachChildrenOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->node_id)) {
    return node->DetachChildren();
  }
  return false;
}

bool Session::ApplySetTagOp(const scenic::SetTagOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->node_id)) {
    return node->SetTagValue(op->tag_value);
  }
  return false;
}

bool Session::ApplySetTranslationOp(const scenic::SetTranslationOpPtr& op) {
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

bool Session::ApplySetScaleOp(const scenic::SetScaleOpPtr& op) {
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

bool Session::ApplySetRotationOp(const scenic::SetRotationOpPtr& op) {
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

bool Session::ApplySetAnchorOp(const scenic::SetAnchorOpPtr& op) {
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

bool Session::ApplySetSizeOp(const scenic::SetSizeOpPtr& op) {
  if (auto layer = resources_.FindResource<Layer>(op->id)) {
    if (IsVariable(op->value)) {
      error_reporter_->ERROR() << "scene_manager::Session::ApplySetSizeOp(): "
                                  "unimplemented for variable value.";
      return false;
    }
    return layer->SetSize(UnwrapVector2(op->value));
  }
  return false;
}

bool Session::ApplySetShapeOp(const scenic::SetShapeOpPtr& op) {
  if (auto node = resources_.FindResource<ShapeNode>(op->node_id)) {
    if (auto shape = resources_.FindResource<Shape>(op->shape_id)) {
      node->SetShape(std::move(shape));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetMaterialOp(const scenic::SetMaterialOpPtr& op) {
  if (auto node = resources_.FindResource<ShapeNode>(op->node_id)) {
    if (auto material = resources_.FindResource<Material>(op->material_id)) {
      node->SetMaterial(std::move(material));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetClipOp(const scenic::SetClipOpPtr& op) {
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
    const scenic::SetHitTestBehaviorOpPtr& op) {
  if (auto node = resources_.FindResource<Node>(op->node_id)) {
    return node->SetHitTestBehavior(op->hit_test_behavior);
  }

  return false;
}

bool Session::ApplySetCameraOp(const scenic::SetCameraOpPtr& op) {
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

bool Session::ApplySetTextureOp(const scenic::SetTextureOpPtr& op) {
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

bool Session::ApplySetColorOp(const scenic::SetColorOpPtr& op) {
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

bool Session::ApplyBindMeshBuffersOp(const scenic::BindMeshBuffersOpPtr& op) {
  auto mesh = resources_.FindResource<MeshShape>(op->mesh_id);
  auto index_buffer = resources_.FindResource<Buffer>(op->index_buffer_id);
  auto vertex_buffer = resources_.FindResource<Buffer>(op->vertex_buffer_id);
  if (mesh && index_buffer && vertex_buffer) {
    return mesh->BindBuffers(
        std::move(index_buffer), op->index_format, op->index_offset,
        op->index_count, std::move(vertex_buffer), op->vertex_format,
        op->vertex_offset, op->vertex_count, Unwrap(op->bounding_box));
  }
  return false;
}

bool Session::ApplyAddLayerOp(const scenic::AddLayerOpPtr& op) {
  auto layer_stack = resources_.FindResource<LayerStack>(op->layer_stack_id);
  auto layer = resources_.FindResource<Layer>(op->layer_id);
  if (layer_stack && layer) {
    return layer_stack->AddLayer(std::move(layer));
  }
  return false;
}

bool Session::ApplySetLayerStackOp(const scenic::SetLayerStackOpPtr& op) {
  auto compositor = resources_.FindResource<Compositor>(op->compositor_id);
  auto layer_stack = resources_.FindResource<LayerStack>(op->layer_stack_id);
  if (compositor && layer_stack) {
    return compositor->SetLayerStack(std::move(layer_stack));
  }
  return false;
}

bool Session::ApplySetRendererOp(const scenic::SetRendererOpPtr& op) {
  auto layer = resources_.FindResource<Layer>(op->layer_id);
  auto renderer = resources_.FindResource<Renderer>(op->renderer_id);

  if (layer && renderer) {
    return layer->SetRenderer(std::move(renderer));
  }
  return false;
}

bool Session::ApplySetRendererParamOp(const scenic::SetRendererParamOpPtr& op) {
  auto renderer = resources_.FindResource<Renderer>(op->renderer_id);
  if (renderer) {
    switch (op->param->which()) {
      case scenic::RendererParam::Tag::SHADOW_TECHNIQUE:
        return renderer->SetShadowTechnique(op->param->get_shadow_technique());
      case scenic::RendererParam::Tag::__UNKNOWN__:
        error_reporter_->ERROR()
            << "scene_manager::Session::ApplySetRendererParamOp(): "
               "unknown param.";
    }
  }
  return false;
}

bool Session::ApplySetEventMaskOp(const scenic::SetEventMaskOpPtr& op) {
  if (auto r = resources_.FindResource<Resource>(op->id)) {
    return r->SetEventMask(op->event_mask);
  }
  return false;
}

bool Session::ApplySetCameraProjectionOp(
    const scenic::SetCameraProjectionOpPtr& op) {
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
    const scenic::SetLightIntensityOpPtr& op) {
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

bool Session::ApplySetLabelOp(const scenic::SetLabelOpPtr& op) {
  if (auto r = resources_.FindResource<Resource>(op->id)) {
    return r->SetLabel(op->label.get());
  }
  return false;
}

bool Session::ApplySetDisableClippingOp(
    const scenic::SetDisableClippingOpPtr& op) {
  if (auto r = resources_.FindResource<Renderer>(op->renderer_id)) {
    r->DisableClipping(op->disable_clipping);
    return true;
  }
  return false;
}

bool Session::ApplyCreateMemory(scenic::ResourceId id,
                                const scenic::MemoryPtr& args) {
  auto memory = CreateMemory(id, args);
  return memory ? resources_.AddResource(id, std::move(memory)) : false;
}

bool Session::ApplyCreateImage(scenic::ResourceId id,
                               const scenic::ImagePtr& args) {
  if (auto memory = resources_.FindResource<Memory>(args->memory_id)) {
    if (auto image = CreateImage(id, std::move(memory), args)) {
      return resources_.AddResource(id, std::move(image));
    }
  }

  return false;
}

bool Session::ApplyCreateImagePipe(scenic::ResourceId id,
                                   const scenic::ImagePipeArgsPtr& args) {
  auto image_pipe = fxl::MakeRefCounted<ImagePipe>(
      this, id, std::move(args->image_pipe_request));
  return resources_.AddResource(id, image_pipe);
}

bool Session::ApplyCreateBuffer(scenic::ResourceId id,
                                const scenic::BufferPtr& args) {
  if (auto memory = resources_.FindResource<Memory>(args->memory_id)) {
    if (auto buffer = CreateBuffer(id, std::move(memory), args->memory_offset,
                                   args->num_bytes)) {
      return resources_.AddResource(id, std::move(buffer));
    }
  }
  return false;
}

bool Session::ApplyCreateScene(scenic::ResourceId id,
                               const scenic::ScenePtr& args) {
  auto scene = CreateScene(id, args);
  return scene ? resources_.AddResource(id, std::move(scene)) : false;
}

bool Session::ApplyCreateCamera(scenic::ResourceId id,
                                const scenic::CameraPtr& args) {
  auto camera = CreateCamera(id, args);
  return camera ? resources_.AddResource(id, std::move(camera)) : false;
}

bool Session::ApplyCreateRenderer(scenic::ResourceId id,
                                  const scenic::RendererPtr& args) {
  auto renderer = CreateRenderer(id, args);
  return renderer ? resources_.AddResource(id, std::move(renderer)) : false;
}

bool Session::ApplyCreateDirectionalLight(
    scenic::ResourceId id,
    const scenic::DirectionalLightPtr& args) {
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

bool Session::ApplyCreateRectangle(scenic::ResourceId id,
                                   const scenic::RectanglePtr& args) {
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
    scenic::ResourceId id,
    const scenic::RoundedRectanglePtr& args) {
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

bool Session::ApplyCreateCircle(scenic::ResourceId id,
                                const scenic::CirclePtr& args) {
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

bool Session::ApplyCreateMesh(scenic::ResourceId id,
                              const scenic::MeshPtr& args) {
  auto mesh = CreateMesh(id);
  return mesh ? resources_.AddResource(id, std::move(mesh)) : false;
}

bool Session::ApplyCreateMaterial(scenic::ResourceId id,
                                  const scenic::MaterialPtr& args) {
  auto material = CreateMaterial(id);
  return material ? resources_.AddResource(id, std::move(material)) : false;
}

bool Session::ApplyCreateClipNode(scenic::ResourceId id,
                                  const scenic::ClipNodePtr& args) {
  auto node = CreateClipNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateEntityNode(scenic::ResourceId id,
                                    const scenic::EntityNodePtr& args) {
  auto node = CreateEntityNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateShapeNode(scenic::ResourceId id,
                                   const scenic::ShapeNodePtr& args) {
  auto node = CreateShapeNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateDisplayCompositor(
    scenic::ResourceId id,
    const scenic::DisplayCompositorPtr& args) {
  auto compositor = CreateDisplayCompositor(id, args);
  return compositor ? resources_.AddResource(id, std::move(compositor)) : false;
}

bool Session::ApplyCreateImagePipeCompositor(
    scenic::ResourceId id,
    const scenic::ImagePipeCompositorPtr& args) {
  auto compositor = CreateImagePipeCompositor(id, args);
  return compositor ? resources_.AddResource(id, std::move(compositor)) : false;
}

bool Session::ApplyCreateLayerStack(scenic::ResourceId id,
                                    const scenic::LayerStackPtr& args) {
  auto layer_stack = CreateLayerStack(id, args);
  return layer_stack ? resources_.AddResource(id, std::move(layer_stack))
                     : false;
}

bool Session::ApplyCreateLayer(scenic::ResourceId id,
                               const scenic::LayerPtr& args) {
  auto layer = CreateLayer(id, args);
  return layer ? resources_.AddResource(id, std::move(layer)) : false;
}

bool Session::ApplyCreateVariable(scenic::ResourceId id,
                                  const scenic::VariablePtr& args) {
  error_reporter_->ERROR()
      << "scene_manager::Session::ApplyCreateVariable(): unimplemented";
  return false;
}

ResourcePtr Session::CreateMemory(scenic::ResourceId id,
                                  const scenic::MemoryPtr& args) {
  vk::Device device = engine()->vk_device();
  switch (args->memory_type) {
    case scenic::MemoryType::VK_DEVICE_MEMORY:
      return GpuMemory::New(this, id, device, args, error_reporter_);
    case scenic::MemoryType::HOST_MEMORY:
      return HostMemory::New(this, id, device, args, error_reporter_);
  }
}

ResourcePtr Session::CreateImage(scenic::ResourceId id,
                                 MemoryPtr memory,
                                 const scenic::ImagePtr& args) {
  return Image::New(this, id, memory, args->info, args->memory_offset,
                    error_reporter_);
}

ResourcePtr Session::CreateBuffer(scenic::ResourceId id,
                                  MemoryPtr memory,
                                  uint32_t memory_offset,
                                  uint32_t num_bytes) {
  if (!memory->IsKindOf<GpuMemory>()) {
    // TODO(MZ-273): host memory should also be supported.
    error_reporter_->ERROR() << "scene_manager::Session::CreateBuffer(): "
                                "memory must be of type "
                                "scenic.MemoryType.VK_DEVICE_MEMORY";
    return ResourcePtr();
  }

  auto gpu_memory = memory->As<GpuMemory>();
  if (memory_offset + num_bytes > gpu_memory->size()) {
    error_reporter_->ERROR() << "scene_manager::Session::CreateBuffer(): "
                                "buffer does not fit within memory (buffer "
                                "offset: "
                             << memory_offset << ", buffer size: " << num_bytes
                             << ", memory size: " << gpu_memory->size() << ")";
    return ResourcePtr();
  }

  return fxl::MakeRefCounted<Buffer>(this, id, std::move(gpu_memory), num_bytes,
                                     memory_offset);
}

ResourcePtr Session::CreateScene(scenic::ResourceId id,
                                 const scenic::ScenePtr& args) {
  return fxl::MakeRefCounted<Scene>(this, id);
}

ResourcePtr Session::CreateCamera(scenic::ResourceId id,
                                  const scenic::CameraPtr& args) {
  if (auto scene = resources_.FindResource<Scene>(args->scene_id)) {
    return fxl::MakeRefCounted<Camera>(this, id, std::move(scene));
  }
  return ResourcePtr();
}

ResourcePtr Session::CreateRenderer(scenic::ResourceId id,
                                    const scenic::RendererPtr& args) {
  return fxl::MakeRefCounted<Renderer>(this, id);
}

ResourcePtr Session::CreateDirectionalLight(scenic::ResourceId id,
                                            escher::vec3 direction,
                                            float intensity) {
  return fxl::MakeRefCounted<DirectionalLight>(this, id, direction, intensity);
}

ResourcePtr Session::CreateClipNode(scenic::ResourceId id,
                                    const scenic::ClipNodePtr& args) {
  error_reporter_->ERROR() << "scene_manager::Session::CreateClipNode(): "
                              "unimplemented.";
  return ResourcePtr();
}

ResourcePtr Session::CreateEntityNode(scenic::ResourceId id,
                                      const scenic::EntityNodePtr& args) {
  return fxl::MakeRefCounted<EntityNode>(this, id);
}

ResourcePtr Session::CreateShapeNode(scenic::ResourceId id,
                                     const scenic::ShapeNodePtr& args) {
  return fxl::MakeRefCounted<ShapeNode>(this, id);
}

ResourcePtr Session::CreateDisplayCompositor(
    scenic::ResourceId id,
    const scenic::DisplayCompositorPtr& args) {
  Display* display = engine()->display_manager()->default_display();
  if (!display) {
    error_reporter_->ERROR() << "There is no default display available.";
    return nullptr;
  }

  if (display->is_claimed()) {
    error_reporter_->ERROR() << "The default display has already been claimed "
                                "by another compositor.";
    return nullptr;
  }
  return fxl::MakeRefCounted<DisplayCompositor>(
      this, id, display, engine()->CreateDisplaySwapchain(display));
}

ResourcePtr Session::CreateImagePipeCompositor(
    scenic::ResourceId id,
    const scenic::ImagePipeCompositorPtr& args) {
  // TODO(MZ-179)
  error_reporter_->ERROR()
      << "scene_manager::Session::ApplyCreateImagePipeCompositor() "
         "is unimplemented (MZ-179)";
  return ResourcePtr();
}

ResourcePtr Session::CreateLayerStack(scenic::ResourceId id,
                                      const scenic::LayerStackPtr& args) {
  return fxl::MakeRefCounted<LayerStack>(this, id);
}

ResourcePtr Session::CreateLayer(scenic::ResourceId id,
                                 const scenic::LayerPtr& args) {
  return fxl::MakeRefCounted<Layer>(this, id);
}

ResourcePtr Session::CreateCircle(scenic::ResourceId id, float initial_radius) {
  return fxl::MakeRefCounted<CircleShape>(this, id, initial_radius);
}

ResourcePtr Session::CreateRectangle(scenic::ResourceId id,
                                     float width,
                                     float height) {
  return fxl::MakeRefCounted<RectangleShape>(this, id, width, height);
}

ResourcePtr Session::CreateRoundedRectangle(scenic::ResourceId id,
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
  escher::MeshSpec mesh_spec{escher::MeshAttribute::kPosition2D |
                             escher::MeshAttribute::kUV};

  return fxl::MakeRefCounted<RoundedRectangleShape>(
      this, id, rect_spec, factory->NewRoundedRect(rect_spec, mesh_spec));
}

ResourcePtr Session::CreateMesh(scenic::ResourceId id) {
  return fxl::MakeRefCounted<MeshShape>(this, id);
}

ResourcePtr Session::CreateMaterial(scenic::ResourceId id) {
  return fxl::MakeRefCounted<Material>(this, id);
}

void Session::TearDown() {
  if (!is_valid_) {
    // TearDown already called.
    return;
  }
  is_valid_ = false;
  resources_.Clear();
  scheduled_image_pipe_updates_ = {};

  // We assume the channel for the associated scenic::Session is closed because
  // SessionHandler closes it before calling this method.
  // The channel *must* be closed before we clear |scheduled_updates_|, since it
  // contains pending callbacks to scenic::Session::Present(); if it were not
  // closed, we would have to invoke those callbacks before destroying them.
  scheduled_updates_ = {};
  fences_to_release_on_next_update_.reset();

  if (resource_count_ != 0) {
    auto exported_count =
        engine()->resource_linker()->NumExportsForSession(this);
    FXL_CHECK(resource_count_ == 0)
        << "Session::TearDown(): Not all resources have been collected. "
           "Exported resources: "
        << exported_count
        << ", total outstanding resources: " << resource_count_;
  }
  error_reporter_ = nullptr;
}

ErrorReporter* Session::error_reporter() const {
  return error_reporter_ ? error_reporter_ : ErrorReporter::Default();
}

bool Session::AssertValueIsOfType(const scenic::ValuePtr& value,
                                  const scenic::Value::Tag* tags,
                                  size_t tag_count) {
  FXL_DCHECK(tag_count > 0);
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

bool Session::ScheduleUpdate(uint64_t presentation_time,
                             ::fidl::Array<scenic::OpPtr> ops,
                             ::fidl::Array<zx::event> acquire_fences,
                             ::fidl::Array<zx::event> release_events,
                             const scenic::Session::PresentCallback& callback) {
  if (is_valid()) {
    uint64_t last_scheduled_presentation_time =
        last_applied_update_presentation_time_;
    if (!scheduled_updates_.empty()) {
      last_scheduled_presentation_time =
          std::max(last_scheduled_presentation_time,
                   scheduled_updates_.back().presentation_time);
    }

    if (presentation_time < last_scheduled_presentation_time) {
      error_reporter_->ERROR()
          << "scene_manager::Session: Present called with out-of-order "
             "presentation time. "
          << "presentation_time=" << presentation_time
          << ", last scheduled presentation time="
          << last_scheduled_presentation_time << ".";
      return false;
    }
    auto acquire_fence_set =
        std::make_unique<FenceSetListener>(std::move(acquire_fences));
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
  return true;
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
  TRACE_DURATION("gfx", "Session::ApplyScheduledUpdates", "id", id_, "time",
                 presentation_time, "interval", presentation_interval);

  if (presentation_time < last_presentation_time_) {
    error_reporter_->ERROR()
        << "scene_manager::Session: ApplyScheduledUpdates called with "
           "presentation_time="
        << presentation_time << ", which is less than last_presentation_time_="
        << last_presentation_time_ << ".";
    return false;
  }

  bool needs_render = false;
  while (!scheduled_updates_.empty() &&
         scheduled_updates_.front().presentation_time <= presentation_time &&
         scheduled_updates_.front().acquire_fences->ready()) {
    if (ApplyUpdate(&scheduled_updates_.front())) {
      needs_render = true;
      auto info = scenic::PresentationInfo::New();
      info->presentation_time = presentation_time;
      info->presentation_interval = presentation_interval;
      scheduled_updates_.front().present_callback(std::move(info));

      FXL_DCHECK(last_applied_update_presentation_time_ <=
                 scheduled_updates_.front().presentation_time);
      last_applied_update_presentation_time_ =
          scheduled_updates_.front().presentation_time;

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
      FXL_LOG(WARNING) << "mozart::Session::ApplyScheduledUpdates(): "
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
         scheduled_image_pipe_updates_.top().presentation_time <=
             presentation_time) {
    needs_render = scheduled_image_pipe_updates_.top().image_pipe->Update(
        presentation_time, presentation_interval);
    scheduled_image_pipe_updates_.pop();
  }

  return needs_render;
}

void Session::EnqueueEvent(scenic::EventPtr event) {
  if (is_valid()) {
    FXL_DCHECK(event);
    if (buffered_events_.empty()) {
      fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
          [weak = weak_factory_.GetWeakPtr()] {
            if (weak)
              weak->FlushEvents();
          });
    }
    buffered_events_.push_back(std::move(event));
  }
}

void Session::FlushEvents() {
  if (!buffered_events_.empty() && event_reporter_) {
    event_reporter_->SendEvents(std::move(buffered_events_));
  }
}

bool Session::ApplyUpdate(Session::Update* update) {
  TRACE_DURATION("gfx", "Session::ApplyUpdate");
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
                      scenic::vec3Ptr ray_origin,
                      scenic::vec3Ptr ray_direction,
                      const scenic::Session::HitTestCallback& callback) {
  fidl::Array<scenic::HitPtr> wrapped_hits;
  if (auto node = resources_.FindResource<Node>(node_id)) {
    HitTester hit_tester;
    std::vector<Hit> hits = hit_tester.HitTest(
        node.get(), escher::ray4{escher::vec4(Unwrap(ray_origin), 1.f),
                                 escher::vec4(Unwrap(ray_direction), 0.f)});
    wrapped_hits.resize(hits.size());
    for (size_t i = 0; i < hits.size(); i++) {
      wrapped_hits[i] = scenic::Hit::New();
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
  FXL_DCHECK(!is_valid());
}

}  // namespace scene_manager
