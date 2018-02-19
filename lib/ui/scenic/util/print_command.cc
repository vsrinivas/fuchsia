// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/util/print_command.h"

#include "lib/fxl/logging.h"

using fuchsia::ui::gfx::Command;
using fuchsia::ui::gfx::CommandPtr;
using fuchsia::ui::gfx::RendererParam;
using fuchsia::ui::gfx::ResourceArgs;
using fuchsia::ui::gfx::ShadowTechnique;
using fuchsia::ui::gfx::Value;

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::Command& command) {
  switch (command.Which()) {
    case Command::Tag::kCreateResource:
      return stream << command.create_resource();
    case Command::Tag::kExportResource:
      return stream << "ExportResource";
    case Command::Tag::kImportResource:
      return stream << "ImportResource";
    case Command::Tag::kReleaseResource:
      return stream << "ReleaseResource";
    case Command::Tag::kSetTranslation:
      return stream << "SetTranslation";
    case Command::Tag::kSetScale:
      return stream << "SetScale";
    case Command::Tag::kSetRotation:
      return stream << "SetRotation";
    case Command::Tag::kSetAnchor:
      return stream << "SetAnchor";
    case Command::Tag::kSetSize:
      return stream << "SetSize";
    case Command::Tag::kSetTag:
      return stream << "SetTag";
    case Command::Tag::kAddChild:
      return stream << "AddChild";
    case Command::Tag::kAddPart:
      return stream << "AddPart";
    case Command::Tag::kDetach:
      return stream << "Detach";
    case Command::Tag::kDetachChildren:
      return stream << "DetachChildren";
    case Command::Tag::kSetOpacity:
      return stream << "SetOpacity";
    case Command::Tag::kSetShape:
      return stream << "SetShape";
    case Command::Tag::kSetMaterial:
      return stream << "SetMaterial";
    case Command::Tag::kSetClip:
      return stream << "SetClip";
    case Command::Tag::kSetHitTestBehavior:
      return stream << "SetHitTestBehavior";
    case Command::Tag::kSetSpaceProperties:
      return stream << "SetSpaceProperties";
    case Command::Tag::kSetCamera:
      return stream << "SetCamera";
    case Command::Tag::kSetCameraTransform:
      return stream << "SetCameraTransform";
    case Command::Tag::kSetStereoCameraProjection:
      return stream << "SetStereoCameraProjection";
    case Command::Tag::kSetCameraProjection:
      return stream << "SetCameraProjection";
    case fuchsia::ui::gfx::Command::Tag::kSetCameraPoseBuffer:
      return stream << "SetCameraPoseBuffer";
    case Command::Tag::kSetLightColor:
      return stream << "SetLightColor";
    case Command::Tag::kSetLightDirection:
      return stream << "SetLightDirection";
    case Command::Tag::kAddLight:
      return stream << "AddLight";
    case Command::Tag::kDetachLight:
      return stream << "DetachLight";
    case Command::Tag::kDetachLights:
      return stream << "DetachLights";
    case Command::Tag::kSetTexture:
      return stream << "SetTexture";
    case Command::Tag::kSetColor:
      return stream << "SetColor";
    case Command::Tag::kBindMeshBuffers:
      return stream << "BindMeshBuffers";
    case Command::Tag::kAddLayer:
      return stream << "AddLayer";
    case Command::Tag::kRemoveLayer:
      return stream << "RemoveLayer";
    case Command::Tag::kRemoveAllLayers:
      return stream << "RemoveAllLayers";
    case Command::Tag::kSetLayerStack:
      return stream << "SetLayerStack";
    case Command::Tag::kSetRenderer:
      return stream << "SetRenderer";
    case Command::Tag::kSetRendererParam:
      return stream << command.set_renderer_param();
    case Command::Tag::kSetEventMask:
      return stream << "SetEventMask";
    case Command::Tag::kSetLabel:
      return stream << "SetLabel";
    case Command::Tag::kSetDisableClipping:
      return stream << "SetDisableClipping";
    case Command::Tag::Invalid:
      return stream << "Invalid";
  }
}

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::CreateResourceCmd& command) {
  stream << "CreateResourceCmd(id:" << command.id << " ";
  switch (command.resource.Which()) {
    case ResourceArgs::Tag::kMemory:
      stream << "Memory";
      break;
    case ResourceArgs::Tag::kImage:
      stream << "Image";
      break;
    case ResourceArgs::Tag::kImagePipe:
      stream << "ImagePipe";
      break;
    case ResourceArgs::Tag::kBuffer:
      stream << "Buffer";
      break;
    case ResourceArgs::Tag::kScene:
      stream << "Scene";
      break;
    case ResourceArgs::Tag::kCamera:
      stream << "Camera";
      break;
    case ResourceArgs::Tag::kStereoCamera:
      stream << "StereoCamera";
      break;
    case ResourceArgs::Tag::kRenderer:
      stream << "Renderer";
      break;
    case ResourceArgs::Tag::kAmbientLight:
      stream << "AmbientLight";
      break;
    case ResourceArgs::Tag::kDirectionalLight:
      stream << "DirectionalLight";
      break;
    case ResourceArgs::Tag::kRectangle:
      stream << "Rectangle";
      break;
    case ResourceArgs::Tag::kRoundedRectangle:
      stream << "RoundedRectangle";
      break;
    case ResourceArgs::Tag::kCircle:
      stream << "Circle";
      break;
    case ResourceArgs::Tag::kMesh:
      stream << "Mesh";
      break;
    case ResourceArgs::Tag::kMaterial:
      stream << "Material";
      break;
    case ResourceArgs::Tag::kClipNode:
      stream << "ClipNode";
      break;
    case ResourceArgs::Tag::kEntityNode:
      stream << "EntityNode";
      break;
    case ResourceArgs::Tag::kOpacityNode:
      stream << "OpacityNode";
      break;
    case ResourceArgs::Tag::kShapeNode:
      stream << "ShapeNode";
      break;
    case ResourceArgs::Tag::kSpaceNode:
      stream << "SpaceNode";
      break;
    case ResourceArgs::Tag::kSpaceHolderNode:
      stream << "SpaceHolderNode";
      break;
    case ResourceArgs::Tag::kDisplayCompositor:
      stream << "DisplayCompositor";
      break;
    case ResourceArgs::Tag::kImagePipeCompositor:
      stream << "ImagePipeCompositor";
      break;
    case ResourceArgs::Tag::kLayerStack:
      stream << "LayerStack";
      break;
    case ResourceArgs::Tag::kLayer:
      stream << "Layer";
      break;
    case ResourceArgs::Tag::kVariable:
      stream << "Variable";
      break;
    case ResourceArgs::Tag::Invalid:
      stream << "Unknown";
      break;
  }
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::SetRendererParamCmd& command) {
  stream << "SetRendererParamCmd(id=" << command.renderer_id << " ";
  switch (command.param.Which()) {
    case RendererParam::Tag::kShadowTechnique:
      stream << "shadow_technique=";
      switch (command.param.shadow_technique()) {
        case ShadowTechnique::UNSHADOWED:
          stream << "Unshadowed";
          break;
        case ShadowTechnique::SCREEN_SPACE:
          stream << "ScreenSpace";
          break;
        case ShadowTechnique::SHADOW_MAP:
          stream << "ShadowMap";
          break;
        case ShadowTechnique::MOMENT_SHADOW_MAP:
          stream << "MomentShadowMap";
          break;
      }
      break;
    case RendererParam::Tag::kRenderFrequency:
      stream << "render_frequency=";
      switch (command.param.render_frequency()) {
        case fuchsia::ui::gfx::RenderFrequency::WHEN_REQUESTED:
          stream << "WhenRequested";
          break;
        case fuchsia::ui::gfx::RenderFrequency::CONTINUOUSLY:
          stream << "Continuous";
          break;
      }
    case RendererParam::Tag::Invalid:
      stream << "Invalid";
      break;
  }
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::SetTextureCmd& command) {
  stream << "SetTextureCmd(id:" << command.material_id
         << " texture: " << command.texture_id;
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::SetColorCmd& command) {
  stream << "SetColorCmd(id:" << command.material_id;
  return stream << ")";
}

std::ostream& operator<<(std::ostream& stream,
                         const fuchsia::ui::gfx::Value::Tag& tag) {
  switch (tag) {
    case Value::Tag::kVector1:
      return stream << "vec1";
    case Value::Tag::kVector2:
      return stream << "vec2";
    case Value::Tag::kVector3:
      return stream << "vec3";
    case Value::Tag::kVector4:
      return stream << "vec4";
    case Value::Tag::kMatrix4x4:
      return stream << "mat4";
    case Value::Tag::kColorRgb:
      return stream << "rgb";
    case Value::Tag::kColorRgba:
      return stream << "rgba";
    case Value::Tag::kDegrees:
      return stream << "degrees";
    case Value::Tag::kQuaternion:
      return stream << "quat";
    case Value::Tag::kTransform:
      return stream << "transform";
    case Value::Tag::kVariableId:
      return stream << "variable";
    case Value::Tag::Invalid:
      return stream << "Invalid";
  }
}
