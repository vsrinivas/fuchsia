// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/util/print_command.h"

using ui::gfx::Command;
using ui::gfx::CommandPtr;
using ui::gfx::RendererParam;
using ui::gfx::ResourceArgs;
using ui::gfx::ShadowTechnique;
using ui::gfx::Value;

std::ostream& operator<<(std::ostream& stream,
                         const ui::gfx::CommandPtr& command) {
  switch (command->which()) {
    case Command::Tag::CREATE_RESOURCE:
      return stream << command->get_create_resource();
    case Command::Tag::EXPORT_RESOURCE:
      return stream << "EXPORT_RESOURCE";
    case Command::Tag::IMPORT_RESOURCE:
      return stream << "IMPORT_RESOURCE";
    case Command::Tag::RELEASE_RESOURCE:
      return stream << "RELEASE_RESOURCE";
    case Command::Tag::SET_TRANSLATION:
      return stream << "SET_TRANSLATION";
    case Command::Tag::SET_SCALE:
      return stream << "SET_SCALE";
    case Command::Tag::SET_ROTATION:
      return stream << "SET_ROTATION";
    case Command::Tag::SET_ANCHOR:
      return stream << "SET_ANCHOR";
    case Command::Tag::SET_SIZE:
      return stream << "SET_SIZE";
    case Command::Tag::SET_TAG:
      return stream << "SET_TAG";
    case Command::Tag::ADD_CHILD:
      return stream << "ADD_CHILD";
    case Command::Tag::ADD_PART:
      return stream << "ADD_PART";
    case Command::Tag::DETACH:
      return stream << "DETACH";
    case Command::Tag::DETACH_CHILDREN:
      return stream << "DETACH_CHILDREN";
    case Command::Tag::SET_SHAPE:
      return stream << "SET_SHAPE";
    case Command::Tag::SET_MATERIAL:
      return stream << "SET_MATERIAL";
    case Command::Tag::SET_CLIP:
      return stream << "SET_CLIP";
    case Command::Tag::SET_HIT_TEST_BEHAVIOR:
      return stream << "SET_HIT_TEST_BEHAVIOR";
    case Command::Tag::SET_CAMERA:
      return stream << "SET_CAMERA";
    case Command::Tag::SET_CAMERA_PROJECTION:
      return stream << "SET_CAMERA_PROJECTION";
    case ui::gfx::Command::Tag::SET_CAMERA_POSE_BUFFER:
      return stream << "SET_CAMERA_POSE_BUFFER";
    case Command::Tag::SET_LIGHT_COLOR:
      return stream << "SET_LIGHT_COLOR";
    case Command::Tag::SET_LIGHT_DIRECTION:
      return stream << "SET_LIGHT_DIRECTION";
    case Command::Tag::ADD_LIGHT:
      return stream << "ADD_LIGHT";
    case Command::Tag::DETACH_LIGHT:
      return stream << "DETACH_LIGHT";
    case Command::Tag::DETACH_LIGHTS:
      return stream << "DETACH_LIGHTS";
    case Command::Tag::SET_TEXTURE:
      return stream << "SET_TEXTURE";
    case Command::Tag::SET_COLOR:
      return stream << "SET_COLOR";
    case Command::Tag::BIND_MESH_BUFFERS:
      return stream << "BIND_MESH_BUFFERS";
    case Command::Tag::ADD_LAYER:
      return stream << "ADD_LAYER";
    case Command::Tag::SET_LAYER_STACK:
      return stream << "SET_LAYER_STACK";
    case Command::Tag::SET_RENDERER:
      return stream << "SET_RENDERER";
    case Command::Tag::SET_RENDERER_PARAM:
      return stream << command->get_set_renderer_param();
    case Command::Tag::SET_EVENT_MASK:
      return stream << "SET_EVENT_MASK";
    case Command::Tag::SET_LABEL:
      return stream << "SET_LABEL";
    case Command::Tag::SET_DISABLE_CLIPPING:
      return stream << "SET_DISABLE_CLIPPING";
    case Command::Tag::__UNKNOWN__:
      return stream << "__UNKNOWN__";
  }
}

std::ostream& operator<<(std::ostream& stream,
                         const ui::gfx::CreateResourceCommandPtr& command) {
  stream << "CreateResourceCommand(id:" << command->id << " ";
  switch (command->resource->which()) {
    case ResourceArgs::Tag::MEMORY:
      stream << "Memory";
      break;
    case ResourceArgs::Tag::IMAGE:
      stream << "Image";
      break;
    case ResourceArgs::Tag::IMAGE_PIPE:
      stream << "ImagePipe";
      break;
    case ResourceArgs::Tag::BUFFER:
      stream << "Buffer";
      break;
    case ResourceArgs::Tag::SCENE:
      stream << "Scene";
      break;
    case ResourceArgs::Tag::CAMERA:
      stream << "Camera";
      break;
    case ResourceArgs::Tag::RENDERER:
      stream << "Renderer";
      break;
    case ResourceArgs::Tag::AMBIENT_LIGHT:
      stream << "AmbientLight";
      break;
    case ResourceArgs::Tag::DIRECTIONAL_LIGHT:
      stream << "DirectionalLight";
      break;
    case ResourceArgs::Tag::RECTANGLE:
      stream << "Rectangle";
      break;
    case ResourceArgs::Tag::ROUNDED_RECTANGLE:
      stream << "RoundedRectangle";
      break;
    case ResourceArgs::Tag::CIRCLE:
      stream << "Circle";
      break;
    case ResourceArgs::Tag::MESH:
      stream << "Mesh";
      break;
    case ResourceArgs::Tag::MATERIAL:
      stream << "Material";
      break;
    case ResourceArgs::Tag::CLIP_NODE:
      stream << "ClipNode";
      break;
    case ResourceArgs::Tag::ENTITY_NODE:
      stream << "EntityNode";
      break;
    case ResourceArgs::Tag::SHAPE_NODE:
      stream << "ShapeNode";
      break;
    case ResourceArgs::Tag::DISPLAY_COMPOSITOR:
      stream << "DisplayCompositor";
      break;
    case ResourceArgs::Tag::IMAGE_PIPE_COMPOSITOR:
      stream << "ImagePipeCompositor";
      break;
    case ResourceArgs::Tag::LAYER_STACK:
      stream << "LayerStack";
      break;
    case ResourceArgs::Tag::LAYER:
      stream << "Layer";
      break;
    case ResourceArgs::Tag::VARIABLE:
      stream << "Variable";
      break;
    case ResourceArgs::Tag::__UNKNOWN__:
      stream << "__UNKNOWN__";
      break;
  }
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const ui::gfx::SetRendererParamCommandPtr& command) {
  stream << "SetRendererParamCommand(id=" << command->renderer_id << " ";
  switch (command->param->which()) {
    case RendererParam::Tag::SHADOW_TECHNIQUE:
      stream << "shadow_technique=";
      switch (command->param->get_shadow_technique()) {
        case ShadowTechnique::UNSHADOWED:
          stream << "UNSHADOWED";
          break;
        case ShadowTechnique::SCREEN_SPACE:
          stream << "SCREEN_SPACE";
          break;
        case ShadowTechnique::SHADOW_MAP:
          stream << "SHADOW_MAP";
          break;
        case ShadowTechnique::MOMENT_SHADOW_MAP:
          stream << "MOMENT_SHADOW_MAP";
          break;
      }
      break;
    case RendererParam::Tag::__UNKNOWN__:
      stream << "__UNKNOWN__";
      break;
  }
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const ui::gfx::SetTextureCommandPtr& command) {
  stream << "SetTextureCommand(id:" << command->material_id
         << " texture: " << command->texture_id;
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const ui::gfx::SetColorCommandPtr& command) {
  stream << "SetColorCommand(id:" << command->material_id;
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream, const ui::gfx::Value::Tag& tag) {
  switch (tag) {
    case Value::Tag::VECTOR1:
      return stream << "vec1";
    case Value::Tag::VECTOR2:
      return stream << "vec2";
    case Value::Tag::VECTOR3:
      return stream << "vec3";
    case Value::Tag::VECTOR4:
      return stream << "vec4";
    case Value::Tag::MATRIX4X4:
      return stream << "mat4";
    case Value::Tag::COLOR_RGB:
      return stream << "rgb";
    case Value::Tag::COLOR_RGBA:
      return stream << "rgba";
    case Value::Tag::DEGREES:
      return stream << "degrees";
    case Value::Tag::QUATERNION:
      return stream << "quat";
    case Value::Tag::TRANSFORM:
      return stream << "transform";
    case Value::Tag::VARIABLE_ID:
      return stream << "variable";
    case Value::Tag::__UNKNOWN__:
      return stream << "__UNKNOWN__";
  }
}
