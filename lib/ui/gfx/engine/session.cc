// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/session.h"

#include <trace/event.h>
#include <zx/time.h>

#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/resources/buffer.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/compositor/display_compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/gpu_memory.h"
#include "garnet/lib/ui/gfx/resources/host_memory.h"
#include "garnet/lib/ui/gfx/resources/image.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/resources/image_pipe_handler.h"
#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"
#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/nodes/scene.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"
#include "garnet/lib/ui/gfx/resources/shapes/circle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/mesh_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/variable.h"
#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "garnet/lib/ui/gfx/util/wrap.h"
#include "lib/ui/gfx/fidl/types.fidl.h"

#include "lib/escher/hmd/pose_buffer.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/shape/rounded_rect_factory.h"
#include "lib/escher/util/type_utils.h"

namespace scenic {
namespace gfx {

namespace {

// Makes it convenient to check that a value is constant and of a specific type,
// or a variable.
// TODO: There should also be a convenient way of type-checking a variable;
// this will necessarily involve looking up the value in the ResourceMap.
constexpr std::array<ui::gfx::Value::Tag, 2> kFloatValueTypes{
    {ui::gfx::Value::Tag::VECTOR1, ui::gfx::Value::Tag::VARIABLE_ID}};

// Converts the provided vector of scene_manager hits into a fidl array of
// HitPtrs.
f1dl::Array<ui::gfx::HitPtr> WrapHits(const std::vector<Hit>& hits) {
  f1dl::Array<ui::gfx::HitPtr> wrapped_hits;
  wrapped_hits.resize(hits.size());
  for (size_t i = 0; i < hits.size(); ++i) {
    const Hit& hit = hits[i];
    ui::gfx::HitPtr wrapped_hit = ui::gfx::Hit::New();
    wrapped_hit->tag_value = hit.tag_value;
    wrapped_hit->ray_origin = Wrap(hit.ray.origin);
    wrapped_hit->ray_direction = Wrap(hit.ray.direction);
    wrapped_hit->inverse_transform = Wrap(hit.inverse_transform);
    wrapped_hit->distance = hit.distance;
    wrapped_hits->at(i) = std::move(wrapped_hit);
  }
  return wrapped_hits;
}

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

bool Session::ApplyCommand(const ui::gfx::CommandPtr& command) {
  switch (command->which()) {
    case ui::gfx::Command::Tag::CREATE_RESOURCE:
      return ApplyCreateResourceCommand(command->get_create_resource());
    case ui::gfx::Command::Tag::RELEASE_RESOURCE:
      return ApplyReleaseResourceCommand(command->get_release_resource());
    case ui::gfx::Command::Tag::EXPORT_RESOURCE:
      return ApplyExportResourceCommand(command->get_export_resource());
    case ui::gfx::Command::Tag::IMPORT_RESOURCE:
      return ApplyImportResourceCommand(command->get_import_resource());
    case ui::gfx::Command::Tag::ADD_CHILD:
      return ApplyAddChildCommand(command->get_add_child());
    case ui::gfx::Command::Tag::ADD_PART:
      return ApplyAddPartCommand(command->get_add_part());
    case ui::gfx::Command::Tag::DETACH:
      return ApplyDetachCommand(command->get_detach());
    case ui::gfx::Command::Tag::DETACH_CHILDREN:
      return ApplyDetachChildrenCommand(command->get_detach_children());
    case ui::gfx::Command::Tag::SET_TAG:
      return ApplySetTagCommand(command->get_set_tag());
    case ui::gfx::Command::Tag::SET_TRANSLATION:
      return ApplySetTranslationCommand(command->get_set_translation());
    case ui::gfx::Command::Tag::SET_SCALE:
      return ApplySetScaleCommand(command->get_set_scale());
    case ui::gfx::Command::Tag::SET_ROTATION:
      return ApplySetRotationCommand(command->get_set_rotation());
    case ui::gfx::Command::Tag::SET_ANCHOR:
      return ApplySetAnchorCommand(command->get_set_anchor());
    case ui::gfx::Command::Tag::SET_SIZE:
      return ApplySetSizeCommand(command->get_set_size());
    case ui::gfx::Command::Tag::SET_SHAPE:
      return ApplySetShapeCommand(command->get_set_shape());
    case ui::gfx::Command::Tag::SET_MATERIAL:
      return ApplySetMaterialCommand(command->get_set_material());
    case ui::gfx::Command::Tag::SET_CLIP:
      return ApplySetClipCommand(command->get_set_clip());
    case ui::gfx::Command::Tag::SET_HIT_TEST_BEHAVIOR:
      return ApplySetHitTestBehaviorCommand(
          command->get_set_hit_test_behavior());
    case ui::gfx::Command::Tag::SET_CAMERA:
      return ApplySetCameraCommand(command->get_set_camera());
    case ui::gfx::Command::Tag::SET_CAMERA_PROJECTION:
      return ApplySetCameraProjectionCommand(
          command->get_set_camera_projection());
    case ui::gfx::Command::Tag::SET_CAMERA_POSE_BUFFER:
      return ApplySetCameraPoseBufferCommand(
          command->get_set_camera_pose_buffer());
    case ui::gfx::Command::Tag::SET_LIGHT_COLOR:
      return ApplySetLightColorCommand(command->get_set_light_color());
    case ui::gfx::Command::Tag::SET_LIGHT_DIRECTION:
      return ApplySetLightDirectionCommand(command->get_set_light_direction());
    case ui::gfx::Command::Tag::ADD_LIGHT:
      return ApplyAddLightCommand(command->get_add_light());
    case ui::gfx::Command::Tag::DETACH_LIGHT:
      return ApplyDetachLightCommand(command->get_detach_light());
    case ui::gfx::Command::Tag::DETACH_LIGHTS:
      return ApplyDetachLightsCommand(command->get_detach_lights());
    case ui::gfx::Command::Tag::SET_TEXTURE:
      return ApplySetTextureCommand(command->get_set_texture());
    case ui::gfx::Command::Tag::SET_COLOR:
      return ApplySetColorCommand(command->get_set_color());
    case ui::gfx::Command::Tag::BIND_MESH_BUFFERS:
      return ApplyBindMeshBuffersCommand(command->get_bind_mesh_buffers());
    case ui::gfx::Command::Tag::ADD_LAYER:
      return ApplyAddLayerCommand(command->get_add_layer());
    case ui::gfx::Command::Tag::SET_LAYER_STACK:
      return ApplySetLayerStackCommand(command->get_set_layer_stack());
    case ui::gfx::Command::Tag::SET_RENDERER:
      return ApplySetRendererCommand(command->get_set_renderer());
    case ui::gfx::Command::Tag::SET_RENDERER_PARAM:
      return ApplySetRendererParamCommand(command->get_set_renderer_param());
    case ui::gfx::Command::Tag::SET_EVENT_MASK:
      return ApplySetEventMaskCommand(command->get_set_event_mask());
    case ui::gfx::Command::Tag::SET_LABEL:
      return ApplySetLabelCommand(command->get_set_label());
    case ui::gfx::Command::Tag::SET_DISABLE_CLIPPING:
      return ApplySetDisableClippingCommand(
          command->get_set_disable_clipping());
    case ui::gfx::Command::Tag::__UNKNOWN__:
      // FIDL validation should make this impossible.
      FXL_CHECK(false);
      return false;
  }
}

bool Session::ApplyCreateResourceCommand(
    const ui::gfx::CreateResourceCommandPtr& command) {
  const scenic::ResourceId id = command->id;
  if (id == 0) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplyCreateResourceCommand(): invalid ID: "
        << command;
    return false;
  }

  switch (command->resource->which()) {
    case ui::gfx::ResourceArgs::Tag::MEMORY:
      return ApplyCreateMemory(id, command->resource->get_memory());
    case ui::gfx::ResourceArgs::Tag::IMAGE:
      return ApplyCreateImage(id, command->resource->get_image());
    case ui::gfx::ResourceArgs::Tag::IMAGE_PIPE:
      return ApplyCreateImagePipe(id, command->resource->get_image_pipe());
    case ui::gfx::ResourceArgs::Tag::BUFFER:
      return ApplyCreateBuffer(id, command->resource->get_buffer());
    case ui::gfx::ResourceArgs::Tag::SCENE:
      return ApplyCreateScene(id, command->resource->get_scene());
    case ui::gfx::ResourceArgs::Tag::CAMERA:
      return ApplyCreateCamera(id, command->resource->get_camera());
    case ui::gfx::ResourceArgs::Tag::RENDERER:
      return ApplyCreateRenderer(id, command->resource->get_renderer());
    case ui::gfx::ResourceArgs::Tag::AMBIENT_LIGHT:
      return ApplyCreateAmbientLight(id,
                                     command->resource->get_ambient_light());
    case ui::gfx::ResourceArgs::Tag::DIRECTIONAL_LIGHT:
      return ApplyCreateDirectionalLight(
          id, command->resource->get_directional_light());
    case ui::gfx::ResourceArgs::Tag::RECTANGLE:
      return ApplyCreateRectangle(id, command->resource->get_rectangle());
    case ui::gfx::ResourceArgs::Tag::ROUNDED_RECTANGLE:
      return ApplyCreateRoundedRectangle(
          id, command->resource->get_rounded_rectangle());
    case ui::gfx::ResourceArgs::Tag::CIRCLE:
      return ApplyCreateCircle(id, command->resource->get_circle());
    case ui::gfx::ResourceArgs::Tag::MESH:
      return ApplyCreateMesh(id, command->resource->get_mesh());
    case ui::gfx::ResourceArgs::Tag::MATERIAL:
      return ApplyCreateMaterial(id, command->resource->get_material());
    case ui::gfx::ResourceArgs::Tag::CLIP_NODE:
      return ApplyCreateClipNode(id, command->resource->get_clip_node());
    case ui::gfx::ResourceArgs::Tag::ENTITY_NODE:
      return ApplyCreateEntityNode(id, command->resource->get_entity_node());
    case ui::gfx::ResourceArgs::Tag::SHAPE_NODE:
      return ApplyCreateShapeNode(id, command->resource->get_shape_node());
    case ui::gfx::ResourceArgs::Tag::DISPLAY_COMPOSITOR:
      return ApplyCreateDisplayCompositor(
          id, command->resource->get_display_compositor());
    case ui::gfx::ResourceArgs::Tag::IMAGE_PIPE_COMPOSITOR:
      return ApplyCreateImagePipeCompositor(
          id, command->resource->get_image_pipe_compositor());
    case ui::gfx::ResourceArgs::Tag::LAYER_STACK:
      return ApplyCreateLayerStack(id, command->resource->get_layer_stack());
    case ui::gfx::ResourceArgs::Tag::LAYER:
      return ApplyCreateLayer(id, command->resource->get_layer());
    case ui::gfx::ResourceArgs::Tag::VARIABLE:
      return ApplyCreateVariable(id, command->resource->get_variable());
    case ui::gfx::ResourceArgs::Tag::__UNKNOWN__:
      // FIDL validation should make this impossible.
      FXL_CHECK(false);
      return false;
  }
}

bool Session::ApplyReleaseResourceCommand(
    const ui::gfx::ReleaseResourceCommandPtr& command) {
  return resources_.RemoveResource(command->id);
}

bool Session::ApplyExportResourceCommand(
    const ui::gfx::ExportResourceCommandPtr& command) {
  if (!command->token) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplyExportResourceCommand(): "
           "no token provided.";
    return false;
  }
  if (auto resource = resources_.FindResource<Resource>(command->id)) {
    return engine_->resource_linker()->ExportResource(
        resource.get(), std::move(command->token));
  }
  return false;
}

bool Session::ApplyImportResourceCommand(
    const ui::gfx::ImportResourceCommandPtr& command) {
  if (!command->token) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplyImportResourceCommand(): "
           "no token provided.";
    return false;
  }
  ImportPtr import =
      fxl::MakeRefCounted<Import>(this, command->id, command->spec);
  return engine_->resource_linker()->ImportResource(
             import.get(), command->spec, std::move(command->token)) &&
         resources_.AddResource(command->id, std::move(import));
}

bool Session::ApplyAddChildCommand(const ui::gfx::AddChildCommandPtr& command) {
  // Find the parent and child nodes.
  if (auto parent_node = resources_.FindResource<Node>(command->node_id)) {
    if (auto child_node = resources_.FindResource<Node>(command->child_id)) {
      return parent_node->AddChild(std::move(child_node));
    }
  }
  return false;
}

bool Session::ApplyAddPartCommand(const ui::gfx::AddPartCommandPtr& command) {
  // Find the parent and part nodes.
  if (auto parent_node = resources_.FindResource<Node>(command->node_id)) {
    if (auto part_node = resources_.FindResource<Node>(command->part_id)) {
      return parent_node->AddPart(std::move(part_node));
    }
  }
  return false;
}

bool Session::ApplyDetachCommand(const ui::gfx::DetachCommandPtr& command) {
  if (auto resource = resources_.FindResource<Resource>(command->id)) {
    return resource->Detach();
  }
  return false;
}

bool Session::ApplyDetachChildrenCommand(
    const ui::gfx::DetachChildrenCommandPtr& command) {
  if (auto node = resources_.FindResource<Node>(command->node_id)) {
    return node->DetachChildren();
  }
  return false;
}

bool Session::ApplySetTagCommand(const ui::gfx::SetTagCommandPtr& command) {
  if (auto node = resources_.FindResource<Node>(command->node_id)) {
    return node->SetTagValue(command->tag_value);
  }
  return false;
}

bool Session::ApplySetTranslationCommand(
    const ui::gfx::SetTranslationCommandPtr& command) {
  if (auto node = resources_.FindResource<Node>(command->id)) {
    if (IsVariable(command->value)) {
      if (auto variable = resources_.FindVariableResource<Vector3Variable>(
              command->value->variable_id)) {
        return node->SetTranslation(variable);
      }
    } else {
      return node->SetTranslation(UnwrapVector3(command->value));
    }
  }
  return false;
}

bool Session::ApplySetScaleCommand(const ui::gfx::SetScaleCommandPtr& command) {
  if (auto node = resources_.FindResource<Node>(command->id)) {
    if (IsVariable(command->value)) {
      if (auto variable = resources_.FindVariableResource<Vector3Variable>(
              command->value->variable_id)) {
        return node->SetScale(variable);
      }
    } else {
      return node->SetScale(UnwrapVector3(command->value));
    }
  }
  return false;
}

bool Session::ApplySetRotationCommand(
    const ui::gfx::SetRotationCommandPtr& command) {
  if (auto node = resources_.FindResource<Node>(command->id)) {
    if (IsVariable(command->value)) {
      if (auto variable = resources_.FindVariableResource<QuaternionVariable>(
              command->value->variable_id)) {
        return node->SetRotation(variable);
      }
    } else {
      return node->SetRotation(UnwrapQuaternion(command->value));
    }
  }
  return false;
}

bool Session::ApplySetAnchorCommand(
    const ui::gfx::SetAnchorCommandPtr& command) {
  if (auto node = resources_.FindResource<Node>(command->id)) {
    if (IsVariable(command->value)) {
      if (auto variable = resources_.FindVariableResource<Vector3Variable>(
              command->value->variable_id)) {
        return node->SetAnchor(variable);
      }
    }
    return node->SetAnchor(UnwrapVector3(command->value));
  }
  return false;
}

bool Session::ApplySetSizeCommand(const ui::gfx::SetSizeCommandPtr& command) {
  if (auto layer = resources_.FindResource<Layer>(command->id)) {
    if (IsVariable(command->value)) {
      error_reporter_->ERROR()
          << "scenic::gfx::Session::ApplySetSizeCommand(): "
             "unimplemented for variable value.";
      return false;
    }
    return layer->SetSize(UnwrapVector2(command->value));
  }
  return false;
}

bool Session::ApplySetShapeCommand(const ui::gfx::SetShapeCommandPtr& command) {
  if (auto node = resources_.FindResource<ShapeNode>(command->node_id)) {
    if (auto shape = resources_.FindResource<Shape>(command->shape_id)) {
      node->SetShape(std::move(shape));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetMaterialCommand(
    const ui::gfx::SetMaterialCommandPtr& command) {
  if (auto node = resources_.FindResource<ShapeNode>(command->node_id)) {
    if (auto material =
            resources_.FindResource<Material>(command->material_id)) {
      node->SetMaterial(std::move(material));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetClipCommand(const ui::gfx::SetClipCommandPtr& command) {
  if (command->clip_id != 0) {
    // TODO(MZ-167): Support non-zero clip_id.
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetClipCommand(): only "
           "clip_to_self is implemented.";
    return false;
  }

  if (auto node = resources_.FindResource<Node>(command->node_id)) {
    return node->SetClipToSelf(command->clip_to_self);
  }

  return false;
}

bool Session::ApplySetHitTestBehaviorCommand(
    const ui::gfx::SetHitTestBehaviorCommandPtr& command) {
  if (auto node = resources_.FindResource<Node>(command->node_id)) {
    return node->SetHitTestBehavior(command->hit_test_behavior);
  }

  return false;
}

bool Session::ApplySetCameraCommand(
    const ui::gfx::SetCameraCommandPtr& command) {
  if (auto renderer = resources_.FindResource<Renderer>(command->renderer_id)) {
    if (command->camera_id == 0) {
      renderer->SetCamera(nullptr);
      return true;
    } else if (auto camera =
                   resources_.FindResource<Camera>(command->camera_id)) {
      renderer->SetCamera(std::move(camera));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetTextureCommand(
    const ui::gfx::SetTextureCommandPtr& command) {
  if (auto material = resources_.FindResource<Material>(command->material_id)) {
    if (command->texture_id == 0) {
      material->SetTexture(nullptr);
      return true;
    } else if (auto image =
                   resources_.FindResource<ImageBase>(command->texture_id)) {
      material->SetTexture(std::move(image));
      return true;
    }
  }
  return false;
}

bool Session::ApplySetColorCommand(const ui::gfx::SetColorCommandPtr& command) {
  if (auto material = resources_.FindResource<Material>(command->material_id)) {
    if (IsVariable(command->color)) {
      error_reporter_->ERROR()
          << "scenic::gfx::Session::ApplySetColorCommand(): "
             "unimplemented for variable color.";
      return false;
    }

    auto& color = command->color->value;
    float red = static_cast<float>(color->red) / 255.f;
    float green = static_cast<float>(color->green) / 255.f;
    float blue = static_cast<float>(color->blue) / 255.f;
    float alpha = static_cast<float>(color->alpha) / 255.f;
    material->SetColor(red, green, blue, alpha);
    return true;
  }
  return false;
}

bool Session::ApplyBindMeshBuffersCommand(
    const ui::gfx::BindMeshBuffersCommandPtr& command) {
  auto mesh = resources_.FindResource<MeshShape>(command->mesh_id);
  auto index_buffer = resources_.FindResource<Buffer>(command->index_buffer_id);
  auto vertex_buffer =
      resources_.FindResource<Buffer>(command->vertex_buffer_id);
  if (mesh && index_buffer && vertex_buffer) {
    return mesh->BindBuffers(std::move(index_buffer), command->index_format,
                             command->index_offset, command->index_count,
                             std::move(vertex_buffer), command->vertex_format,
                             command->vertex_offset, command->vertex_count,
                             Unwrap(command->bounding_box));
  }
  return false;
}

bool Session::ApplyAddLayerCommand(const ui::gfx::AddLayerCommandPtr& command) {
  auto layer_stack =
      resources_.FindResource<LayerStack>(command->layer_stack_id);
  auto layer = resources_.FindResource<Layer>(command->layer_id);
  if (layer_stack && layer) {
    return layer_stack->AddLayer(std::move(layer));
  }
  return false;
}

bool Session::ApplySetLayerStackCommand(
    const ui::gfx::SetLayerStackCommandPtr& command) {
  auto compositor = resources_.FindResource<Compositor>(command->compositor_id);
  auto layer_stack =
      resources_.FindResource<LayerStack>(command->layer_stack_id);
  if (compositor && layer_stack) {
    return compositor->SetLayerStack(std::move(layer_stack));
  }
  return false;
}

bool Session::ApplySetRendererCommand(
    const ui::gfx::SetRendererCommandPtr& command) {
  auto layer = resources_.FindResource<Layer>(command->layer_id);
  auto renderer = resources_.FindResource<Renderer>(command->renderer_id);

  if (layer && renderer) {
    return layer->SetRenderer(std::move(renderer));
  }
  return false;
}

bool Session::ApplySetRendererParamCommand(
    const ui::gfx::SetRendererParamCommandPtr& command) {
  auto renderer = resources_.FindResource<Renderer>(command->renderer_id);
  if (renderer) {
    switch (command->param->which()) {
      case ui::gfx::RendererParam::Tag::SHADOW_TECHNIQUE:
        return renderer->SetShadowTechnique(
            command->param->get_shadow_technique());
      case ui::gfx::RendererParam::Tag::__UNKNOWN__:
        error_reporter_->ERROR()
            << "scenic::gfx::Session::ApplySetRendererParamCommand(): "
               "unknown param.";
    }
  }
  return false;
}

bool Session::ApplySetEventMaskCommand(
    const ui::gfx::SetEventMaskCommandPtr& command) {
  if (auto r = resources_.FindResource<Resource>(command->id)) {
    return r->SetEventMask(command->event_mask);
  }
  return false;
}

bool Session::ApplySetCameraProjectionCommand(
    const ui::gfx::SetCameraProjectionCommandPtr& command) {
  // TODO(MZ-123): support variables.
  if (IsVariable(command->eye_position) || IsVariable(command->eye_look_at) ||
      IsVariable(command->eye_up) || IsVariable(command->fovy)) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraProjectionCommand(): "
           "unimplemented: variable properties.";
    return false;
  } else if (auto camera =
                 resources_.FindResource<Camera>(command->camera_id)) {
    camera->SetProjection(UnwrapVector3(command->eye_position),
                          UnwrapVector3(command->eye_look_at),
                          UnwrapVector3(command->eye_up),
                          UnwrapFloat(command->fovy));
    return true;
  }
  return false;
}

bool Session::ApplySetCameraPoseBufferCommand(
    const ui::gfx::SetCameraPoseBufferCommandPtr& command) {
  if (command->base_time > zx::clock::get(ZX_CLOCK_MONOTONIC).get()) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraPoseBufferCommand(): "
           "base time not in the past";
    return false;
  }

  auto buffer = resources_.FindResource<Buffer>(command->buffer_id);
  if (!buffer) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraPoseBufferCommand(): "
           "invalid buffer ID";
    return false;
  }

  if (command->num_entries < 1) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraPoseBufferCommand(): "
           "must have at least one entry in the pose buffer";
    return false;
  }

  if (buffer->size() < command->num_entries * sizeof(escher::hmd::Pose)) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraPoseBufferCommand(): "
           "buffer is not large enough";
    return false;
  }

  auto camera = resources_.FindResource<Camera>(command->camera_id);
  if (!camera) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetCameraPoseBufferCommand(): "
           "invalid camera ID";
    return false;
  }

  camera->SetPoseBuffer(buffer, command->num_entries, command->base_time,
                        command->time_interval);

  return true;
}

bool Session::ApplySetLightColorCommand(
    const ui::gfx::SetLightColorCommandPtr& command) {
  // TODO(MZ-123): support variables.
  if (command->color->variable_id) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetLightColorCommand(): "
           "unimplemented: variable color.";
    return false;
  } else if (auto light = resources_.FindResource<Light>(command->light_id)) {
    return light->SetColor(Unwrap(command->color->value));
  }
  return false;
}

bool Session::ApplySetLightDirectionCommand(
    const ui::gfx::SetLightDirectionCommandPtr& command) {
  // TODO(MZ-123): support variables.
  if (command->direction->variable_id) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session::ApplySetLightDirectionCommand(): "
           "unimplemented: variable direction.";
    return false;
  } else if (auto light =
                 resources_.FindResource<DirectionalLight>(command->light_id)) {
    return light->SetDirection(Unwrap(command->direction->value));
  }
  return false;
}

bool Session::ApplyAddLightCommand(const ui::gfx::AddLightCommandPtr& command) {
  if (auto scene = resources_.FindResource<Scene>(command->scene_id)) {
    if (auto light = resources_.FindResource<Light>(command->light_id)) {
      return scene->AddLight(std::move(light));
    }
  }

  error_reporter_->ERROR()
      << "scenic::gfx::Session::ApplyAddLightCommand(): unimplemented.";
  return false;
}

bool Session::ApplyDetachLightCommand(
    const ui::gfx::DetachLightCommandPtr& command) {
  error_reporter_->ERROR()
      << "scenic::gfx::Session::ApplyDetachLightCommand(): unimplemented.";
  return false;
}

bool Session::ApplyDetachLightsCommand(
    const ui::gfx::DetachLightsCommandPtr& command) {
  error_reporter_->ERROR()
      << "scenic::gfx::Session::ApplyDetachLightsCommand(): unimplemented.";
  return false;
}

bool Session::ApplySetLabelCommand(const ui::gfx::SetLabelCommandPtr& command) {
  if (auto r = resources_.FindResource<Resource>(command->id)) {
    return r->SetLabel(command->label.get());
  }
  return false;
}

bool Session::ApplySetDisableClippingCommand(
    const ui::gfx::SetDisableClippingCommandPtr& command) {
  if (auto r = resources_.FindResource<Renderer>(command->renderer_id)) {
    r->DisableClipping(command->disable_clipping);
    return true;
  }
  return false;
}

bool Session::ApplyCreateMemory(scenic::ResourceId id,
                                const ui::gfx::MemoryArgsPtr& args) {
  auto memory = CreateMemory(id, args);
  return memory ? resources_.AddResource(id, std::move(memory)) : false;
}

bool Session::ApplyCreateImage(scenic::ResourceId id,
                               const ui::gfx::ImageArgsPtr& args) {
  if (auto memory = resources_.FindResource<Memory>(args->memory_id)) {
    if (auto image = CreateImage(id, std::move(memory), args)) {
      return resources_.AddResource(id, std::move(image));
    }
  }

  return false;
}

bool Session::ApplyCreateImagePipe(scenic::ResourceId id,
                                   const ui::gfx::ImagePipeArgsPtr& args) {
  auto image_pipe = fxl::MakeRefCounted<ImagePipe>(
      this, id, std::move(args->image_pipe_request));
  return resources_.AddResource(id, image_pipe);
}

bool Session::ApplyCreateBuffer(scenic::ResourceId id,
                                const ui::gfx::BufferArgsPtr& args) {
  if (auto memory = resources_.FindResource<Memory>(args->memory_id)) {
    if (auto buffer = CreateBuffer(id, std::move(memory), args->memory_offset,
                                   args->num_bytes)) {
      return resources_.AddResource(id, std::move(buffer));
    }
  }
  return false;
}

bool Session::ApplyCreateScene(scenic::ResourceId id,
                               const ui::gfx::SceneArgsPtr& args) {
  auto scene = CreateScene(id, args);
  return scene ? resources_.AddResource(id, std::move(scene)) : false;
}

bool Session::ApplyCreateCamera(scenic::ResourceId id,
                                const ui::gfx::CameraArgsPtr& args) {
  auto camera = CreateCamera(id, args);
  return camera ? resources_.AddResource(id, std::move(camera)) : false;
}

bool Session::ApplyCreateRenderer(scenic::ResourceId id,
                                  const ui::gfx::RendererArgsPtr& args) {
  auto renderer = CreateRenderer(id, args);
  return renderer ? resources_.AddResource(id, std::move(renderer)) : false;
}

bool Session::ApplyCreateAmbientLight(
    scenic::ResourceId id,
    const ui::gfx::AmbientLightArgsPtr& args) {
  auto light = CreateAmbientLight(id);
  return light ? resources_.AddResource(id, std::move(light)) : false;
}

bool Session::ApplyCreateDirectionalLight(
    scenic::ResourceId id,
    const ui::gfx::DirectionalLightArgsPtr& args) {
  auto light = CreateDirectionalLight(id);
  return light ? resources_.AddResource(id, std::move(light)) : false;
}

bool Session::ApplyCreateRectangle(scenic::ResourceId id,
                                   const ui::gfx::RectangleArgsPtr& args) {
  if (!AssertValueIsOfType(args->width, kFloatValueTypes) ||
      !AssertValueIsOfType(args->height, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args->width) || IsVariable(args->height)) {
    error_reporter_->ERROR() << "scenic::gfx::Session::ApplyCreateRectangle(): "
                                "unimplemented: variable width/height.";
    return false;
  }

  auto rectangle = CreateRectangle(id, args->width->get_vector1(),
                                   args->height->get_vector1());
  return rectangle ? resources_.AddResource(id, std::move(rectangle)) : false;
}

bool Session::ApplyCreateRoundedRectangle(
    scenic::ResourceId id,
    const ui::gfx::RoundedRectangleArgsPtr& args) {
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
        << "scenic::gfx::Session::ApplyCreateRoundedRectangle(): "
           "unimplemented: variable width/height/radii.";
    return false;
  }

  const float width = args->width->get_vector1();
  const float height = args->height->get_vector1();
  const float top_left_radius = args->top_left_radius->get_vector1();
  const float top_right_radius = args->top_right_radius->get_vector1();
  const float bottom_right_radius = args->bottom_right_radius->get_vector1();
  const float bottom_left_radius = args->bottom_left_radius->get_vector1();

  auto rectangle = CreateRoundedRectangle(id, width, height, top_left_radius,
                                          top_right_radius, bottom_right_radius,
                                          bottom_left_radius);
  return rectangle ? resources_.AddResource(id, std::move(rectangle)) : false;
}

bool Session::ApplyCreateCircle(scenic::ResourceId id,
                                const ui::gfx::CircleArgsPtr& args) {
  if (!AssertValueIsOfType(args->radius, kFloatValueTypes)) {
    return false;
  }

  // TODO(MZ-123): support variables.
  if (IsVariable(args->radius)) {
    error_reporter_->ERROR() << "scenic::gfx::Session::ApplyCreateCircle(): "
                                "unimplemented: variable radius.";
    return false;
  }

  auto circle = CreateCircle(id, args->radius->get_vector1());
  return circle ? resources_.AddResource(id, std::move(circle)) : false;
}

bool Session::ApplyCreateMesh(scenic::ResourceId id,
                              const ui::gfx::MeshArgsPtr& args) {
  auto mesh = CreateMesh(id);
  return mesh ? resources_.AddResource(id, std::move(mesh)) : false;
}

bool Session::ApplyCreateMaterial(scenic::ResourceId id,
                                  const ui::gfx::MaterialArgsPtr& args) {
  auto material = CreateMaterial(id);
  return material ? resources_.AddResource(id, std::move(material)) : false;
}

bool Session::ApplyCreateClipNode(scenic::ResourceId id,
                                  const ui::gfx::ClipNodeArgsPtr& args) {
  auto node = CreateClipNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateEntityNode(scenic::ResourceId id,
                                    const ui::gfx::EntityNodeArgsPtr& args) {
  auto node = CreateEntityNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateShapeNode(scenic::ResourceId id,
                                   const ui::gfx::ShapeNodeArgsPtr& args) {
  auto node = CreateShapeNode(id, args);
  return node ? resources_.AddResource(id, std::move(node)) : false;
}

bool Session::ApplyCreateDisplayCompositor(
    scenic::ResourceId id,
    const ui::gfx::DisplayCompositorArgsPtr& args) {
  auto compositor = CreateDisplayCompositor(id, args);
  return compositor ? resources_.AddResource(id, std::move(compositor)) : false;
}

bool Session::ApplyCreateImagePipeCompositor(
    scenic::ResourceId id,
    const ui::gfx::ImagePipeCompositorArgsPtr& args) {
  auto compositor = CreateImagePipeCompositor(id, args);
  return compositor ? resources_.AddResource(id, std::move(compositor)) : false;
}

bool Session::ApplyCreateLayerStack(scenic::ResourceId id,
                                    const ui::gfx::LayerStackArgsPtr& args) {
  auto layer_stack = CreateLayerStack(id, args);
  return layer_stack ? resources_.AddResource(id, std::move(layer_stack))
                     : false;
}

bool Session::ApplyCreateLayer(scenic::ResourceId id,
                               const ui::gfx::LayerArgsPtr& args) {
  auto layer = CreateLayer(id, args);
  return layer ? resources_.AddResource(id, std::move(layer)) : false;
}

bool Session::ApplyCreateVariable(scenic::ResourceId id,
                                  const ui::gfx::VariableArgsPtr& args) {
  auto variable = CreateVariable(id, args);
  return variable ? resources_.AddResource(id, std::move(variable)) : false;
}

ResourcePtr Session::CreateMemory(scenic::ResourceId id,
                                  const ui::gfx::MemoryArgsPtr& args) {
  vk::Device device = engine()->vk_device();
  switch (args->memory_type) {
    case ui::gfx::MemoryType::VK_DEVICE_MEMORY:
      return GpuMemory::New(this, id, device, args, error_reporter_);
    case ui::gfx::MemoryType::HOST_MEMORY:
      return HostMemory::New(this, id, device, args, error_reporter_);
  }
}

ResourcePtr Session::CreateImage(scenic::ResourceId id,
                                 MemoryPtr memory,
                                 const ui::gfx::ImageArgsPtr& args) {
  return Image::New(this, id, memory, args->info, args->memory_offset,
                    error_reporter_);
}

ResourcePtr Session::CreateBuffer(scenic::ResourceId id,
                                  MemoryPtr memory,
                                  uint32_t memory_offset,
                                  uint32_t num_bytes) {
  if (!memory->IsKindOf<GpuMemory>()) {
    // TODO(MZ-273): host memory should also be supported.
    error_reporter_->ERROR() << "scenic::gfx::Session::CreateBuffer(): "
                                "memory must be of type "
                                "ui.gfx.MemoryType.VK_DEVICE_MEMORY";
    return ResourcePtr();
  }

  auto gpu_memory = memory->As<GpuMemory>();
  if (memory_offset + num_bytes > gpu_memory->size()) {
    error_reporter_->ERROR() << "scenic::gfx::Session::CreateBuffer(): "
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
                                 const ui::gfx::SceneArgsPtr& args) {
  return fxl::MakeRefCounted<Scene>(this, id);
}

ResourcePtr Session::CreateCamera(scenic::ResourceId id,
                                  const ui::gfx::CameraArgsPtr& args) {
  if (auto scene = resources_.FindResource<Scene>(args->scene_id)) {
    return fxl::MakeRefCounted<Camera>(this, id, std::move(scene));
  }
  return ResourcePtr();
}

ResourcePtr Session::CreateRenderer(scenic::ResourceId id,
                                    const ui::gfx::RendererArgsPtr& args) {
  return fxl::MakeRefCounted<Renderer>(this, id);
}

ResourcePtr Session::CreateAmbientLight(scenic::ResourceId id) {
  return fxl::MakeRefCounted<AmbientLight>(this, id);
}

ResourcePtr Session::CreateDirectionalLight(scenic::ResourceId id) {
  return fxl::MakeRefCounted<DirectionalLight>(this, id);
}

ResourcePtr Session::CreateClipNode(scenic::ResourceId id,
                                    const ui::gfx::ClipNodeArgsPtr& args) {
  error_reporter_->ERROR() << "scenic::gfx::Session::CreateClipNode(): "
                              "unimplemented.";
  return ResourcePtr();
}

ResourcePtr Session::CreateEntityNode(scenic::ResourceId id,
                                      const ui::gfx::EntityNodeArgsPtr& args) {
  return fxl::MakeRefCounted<EntityNode>(this, id);
}

ResourcePtr Session::CreateShapeNode(scenic::ResourceId id,
                                     const ui::gfx::ShapeNodeArgsPtr& args) {
  return fxl::MakeRefCounted<ShapeNode>(this, id);
}

ResourcePtr Session::CreateDisplayCompositor(
    scenic::ResourceId id,
    const ui::gfx::DisplayCompositorArgsPtr& args) {
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
    const ui::gfx::ImagePipeCompositorArgsPtr& args) {
  // TODO(MZ-179)
  error_reporter_->ERROR()
      << "scenic::gfx::Session::ApplyCreateImagePipeCompositor() "
         "is unimplemented (MZ-179)";
  return ResourcePtr();
}

ResourcePtr Session::CreateLayerStack(scenic::ResourceId id,
                                      const ui::gfx::LayerStackArgsPtr& args) {
  return fxl::MakeRefCounted<LayerStack>(this, id);
}

ResourcePtr Session::CreateVariable(scenic::ResourceId id,
                                    const ui::gfx::VariableArgsPtr& args) {
  fxl::RefPtr<Variable> variable;
  switch (args->type) {
    case ui::gfx::ValueType::kVector1:
      variable = fxl::MakeRefCounted<FloatVariable>(this, id);
      break;
    case ui::gfx::ValueType::kVector2:
      variable = fxl::MakeRefCounted<Vector2Variable>(this, id);
      break;
    case ui::gfx::ValueType::kVector3:
      variable = fxl::MakeRefCounted<Vector3Variable>(this, id);
      break;
    case ui::gfx::ValueType::kVector4:
      variable = fxl::MakeRefCounted<Vector4Variable>(this, id);
      break;
    case ui::gfx::ValueType::kMatrix4:
      variable = fxl::MakeRefCounted<Matrix4x4Variable>(this, id);
      break;
    case ui::gfx::ValueType::kColorRgb:
      // not yet supported
      variable = nullptr;
      break;
    case ui::gfx::ValueType::kColorRgba:
      // not yet supported
      variable = nullptr;
      break;
    case ui::gfx::ValueType::kQuaternion:
      variable = fxl::MakeRefCounted<QuaternionVariable>(this, id);
      break;
    case ui::gfx::ValueType::kTransform:
      /* variable = fxl::MakeRefCounted<TransformVariable>(this, id); */
      variable = nullptr;
      break;
    case ui::gfx::ValueType::kNone:
      break;
  }
  if (variable && variable->SetValue(args->initial_value)) {
    return variable;
  }
  return nullptr;
}

ResourcePtr Session::CreateLayer(scenic::ResourceId id,
                                 const ui::gfx::LayerArgsPtr& args) {
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
        << "scenic::gfx::Session::CreateRoundedRectangle(): "
           "no RoundedRectFactory available.";
    return ResourcePtr();
  }

  // If radii sum exceeds width or height, scale them down.
  float top_radii_sum = top_left_radius + top_right_radius;
  float top_scale = std::min(width / top_radii_sum, 1.f);

  float bottom_radii_sum = bottom_left_radius + bottom_right_radius;
  float bottom_scale = std::min(width / bottom_radii_sum, 1.f);

  float left_radii_sum = top_left_radius + bottom_left_radius;
  float left_scale = std::min(height / left_radii_sum, 1.f);

  float right_radii_sum = top_right_radius + bottom_right_radius;
  float right_scale = std::min(height / right_radii_sum, 1.f);

  top_left_radius *= std::min(top_scale, left_scale);
  top_right_radius *= std::min(top_scale, right_scale);
  bottom_left_radius *= std::min(bottom_scale, left_scale);
  bottom_right_radius *= std::min(bottom_scale, right_scale);

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

  // We assume the channel for the associated ui::gfx::Session is closed because
  // SessionHandler closes it before calling this method.
  // The channel *must* be closed before we clear |scheduled_updates_|, since it
  // contains pending callbacks to ui::gfx::Session::Present(); if it were not
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

bool Session::AssertValueIsOfType(const ui::gfx::ValuePtr& value,
                                  const ui::gfx::Value::Tag* tags,
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
  error_reporter_->ERROR() << "scenic::gfx::Session: received value of type: "
                           << value->which() << str.str();
  return false;
}

bool Session::ScheduleUpdate(uint64_t presentation_time,
                             ::f1dl::Array<ui::gfx::CommandPtr> commands,
                             ::f1dl::Array<zx::event> acquire_fences,
                             ::f1dl::Array<zx::event> release_events,
                             const ui::Session::PresentCallback& callback) {
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
          << "scenic::gfx::Session: Present called with out-of-order "
             "presentation time. "
          << "presentation_time=" << presentation_time
          << ", last scheduled presentation time="
          << last_scheduled_presentation_time << ".";
      return false;
    }
    auto acquire_fence_set =
        std::make_unique<escher::FenceSetListener>(std::move(acquire_fences));
    // TODO: Consider calling ScheduleUpdateForSession immediately if
    // acquire_fence_set is already ready (which is the case if there are
    // zero acquire fences).

    acquire_fence_set->WaitReadyAsync(
        [ weak = weak_factory_.GetWeakPtr(), presentation_time ] {
          if (weak)
            weak->engine_->session_manager()->ScheduleUpdateForSession(
                presentation_time, SessionPtr(weak.get()));
        });

    scheduled_updates_.push(Update{presentation_time, std::move(commands),
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

    engine_->session_manager()->ScheduleUpdateForSession(presentation_time,
                                                         SessionPtr(this));
  }
}

bool Session::ApplyScheduledUpdates(uint64_t presentation_time,
                                    uint64_t presentation_interval) {
  TRACE_DURATION("gfx", "Session::ApplyScheduledUpdates", "id", id_, "time",
                 presentation_time, "interval", presentation_interval);

  if (presentation_time < last_presentation_time_) {
    error_reporter_->ERROR()
        << "scenic::gfx::Session: ApplyScheduledUpdates called with "
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
      auto info = ui::PresentationInfo::New();
      info->presentation_time = presentation_time;
      info->presentation_interval = presentation_interval;
      scheduled_updates_.front().present_callback(std::move(info));

      FXL_DCHECK(last_applied_update_presentation_time_ <=
                 scheduled_updates_.front().presentation_time);
      last_applied_update_presentation_time_ =
          scheduled_updates_.front().presentation_time;

      for (size_t i = 0; i < fences_to_release_on_next_update_->size(); ++i) {
        engine()->release_fence_signaller()->AddCPUReleaseFence(
            std::move(fences_to_release_on_next_update_->at(i)));
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

void Session::EnqueueEvent(ui::gfx::EventPtr event) {
  if (is_valid()) {
    FXL_DCHECK(event);
    if (buffered_events_->empty()) {
      fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
          [weak = weak_factory_.GetWeakPtr()] {
            if (weak)
              weak->FlushEvents();
          });
    }
    auto scenic_event = ui::Event::New();
    scenic_event->set_gfx(std::move(event));
    buffered_events_.push_back(std::move(scenic_event));
  }
}

void Session::FlushEvents() {
  if (!buffered_events_->empty() && event_reporter_) {
    event_reporter_->SendEvents(std::move(buffered_events_));
  }
}

bool Session::ApplyUpdate(Session::Update* update) {
  TRACE_DURATION("gfx", "Session::ApplyUpdate");
  if (is_valid()) {
    for (auto& command : *update->commands) {
      if (!ApplyCommand(command)) {
        error_reporter_->ERROR()
            << "scenic::gfx::Session::ApplyCommand() failed to apply Command: "
            << command;
        return false;
      }
    }
  }
  return true;

  // TODO: acquire_fences and release_fences should be added to a list that is
  // consumed by the FrameScheduler.
}

void Session::HitTest(uint32_t node_id,
                      ui::gfx::vec3Ptr ray_origin,
                      ui::gfx::vec3Ptr ray_direction,
                      const ui::Session::HitTestCallback& callback) {
  if (auto node = resources_.FindResource<Node>(node_id)) {
    HitTester hit_tester;
    std::vector<Hit> hits = hit_tester.HitTest(
        node.get(), escher::ray4{escher::vec4(Unwrap(ray_origin), 1.f),
                                 escher::vec4(Unwrap(ray_direction), 0.f)});
    callback(WrapHits(hits));
  } else {
    // TODO(MZ-162): Currently the test fails if the node isn't presented yet.
    // Perhaps we should given clients more control over which state of
    // the scene graph will be consulted for hit testing purposes.
    error_reporter_->WARN()
        << "Cannot perform hit test because node " << node_id
        << " does not exist in the currently presented content.";
    callback(nullptr);
  }
}

void Session::HitTestDeviceRay(ui::gfx::vec3Ptr ray_origin,
                               ui::gfx::vec3Ptr ray_direction,
                               const ui::Session::HitTestCallback& callback) {
  escher::ray4 ray =
      escher::ray4{{Unwrap(ray_origin), 1.f}, {Unwrap(ray_direction), 0.f}};

  // The layer stack expects the input to the hit test to be in unscaled device
  // coordinates.
  std::vector<Hit> layer_stack_hits =
      engine_->GetFirstCompositor()->layer_stack()->HitTest(ray, this);

  callback(WrapHits(layer_stack_hits));
}

void Session::BeginTearDown() {
  engine()->session_manager()->TearDownSession(id());
  FXL_DCHECK(!is_valid());
}

}  // namespace gfx
}  // namespace scenic
