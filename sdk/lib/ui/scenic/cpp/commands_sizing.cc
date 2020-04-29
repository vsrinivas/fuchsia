// File is automatically generated; do not modify.
// See tools/fidl/measure-tape/README.md

#include <lib/ui/scenic/cpp/commands_sizing.h>

#include <fuchsia/images/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <zircon/types.h>


namespace measure_tape {
namespace fuchsia {
namespace ui {
namespace scenic {

namespace {

class MeasuringTape {
 public:
  MeasuringTape() = default;

  void Measure(const ::fuchsia::images::ImageInfo& value) {
    num_bytes_ += FIDL_ALIGN(32);
  }

  void Measure(const ::fuchsia::ui::gfx::AddChildCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::AddLayerCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::AddLightCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::AddPartCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::AmbientLightArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::BindMeshBuffersCmd& value) {
    num_bytes_ += FIDL_ALIGN(88);
  }

  void Measure(const ::fuchsia::ui::gfx::BoundingBox& value) {
    num_bytes_ += FIDL_ALIGN(24);
  }

  void Measure(const ::fuchsia::ui::gfx::BufferArgs& value) {
    num_bytes_ += FIDL_ALIGN(12);
  }

  void Measure(const ::fuchsia::ui::gfx::CameraArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::CircleArgs& value) {
    num_bytes_ += FIDL_ALIGN(24);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::CircleArgs& value) {
    MeasureOutOfLine(value.radius);
  }

  void Measure(const ::fuchsia::ui::gfx::ClipNodeArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::ColorRgb& value) {
    num_bytes_ += FIDL_ALIGN(12);
  }

  void Measure(const ::fuchsia::ui::gfx::ColorRgbValue& value) {
    num_bytes_ += FIDL_ALIGN(16);
  }

  void Measure(const ::fuchsia::ui::gfx::ColorRgba& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::ColorRgbaValue& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::Command& value) {
    num_bytes_ += 24;
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::Command& value) {
    switch (value.Which()) {
      case ::fuchsia::ui::gfx::Command::Tag::Invalid:
        MaxOut();
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kAddChild:
        Measure(value.add_child());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kAddLayer:
        Measure(value.add_layer());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kAddLight:
        Measure(value.add_light());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kAddPart:
        Measure(value.add_part());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kBindMeshBuffers:
        Measure(value.bind_mesh_buffers());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kCreateResource:
        Measure(value.create_resource());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kDetach:
        Measure(value.detach());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kDetachChildren:
        Measure(value.detach_children());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kDetachLight:
        Measure(value.detach_light());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kDetachLights:
        Measure(value.detach_lights());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kExportResource:
        Measure(value.export_resource());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kImportResource:
        Measure(value.import_resource());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kReleaseResource:
        Measure(value.release_resource());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kRemoveAllLayers:
        Measure(value.remove_all_layers());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kRemoveLayer:
        Measure(value.remove_layer());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kScene_AddAmbientLight:
        Measure(value.scene__add_ambient_light());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kScene_AddDirectionalLight:
        Measure(value.scene__add_directional_light());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kScene_AddPointLight:
        Measure(value.scene__add_point_light());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSendSizeChangeHintHack:
        Measure(value.send_size_change_hint_hack());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetAnchor:
        Measure(value.set_anchor());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetCamera:
        Measure(value.set_camera());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetCameraClipSpaceTransform:
        Measure(value.set_camera_clip_space_transform());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetCameraPoseBuffer:
        Measure(value.set_camera_pose_buffer());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetCameraProjection:
        Measure(value.set_camera_projection());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetCameraTransform:
        Measure(value.set_camera_transform());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetClip:
        Measure(value.set_clip());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetClipPlanes:
        Measure(value.set_clip_planes());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetColor:
        Measure(value.set_color());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetDisableClipping:
        Measure(value.set_disable_clipping());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetDisplayColorConversion:
        Measure(value.set_display_color_conversion());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetDisplayRotation:
        Measure(value.set_display_rotation());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetEnableViewDebugBounds:
        Measure(value.set_enable_view_debug_bounds());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetEventMask:
        Measure(value.set_event_mask());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetHitTestBehavior:
        Measure(value.set_hit_test_behavior());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetImportFocus:
        Measure(value.set_import_focus());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetLabel:
        Measure(value.set_label());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetLayerStack:
        Measure(value.set_layer_stack());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetLightColor:
        Measure(value.set_light_color());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetLightDirection:
        Measure(value.set_light_direction());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetMaterial:
        Measure(value.set_material());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetOpacity:
        Measure(value.set_opacity());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetPointLightFalloff:
        Measure(value.set_point_light_falloff());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetPointLightPosition:
        Measure(value.set_point_light_position());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetRenderer:
        Measure(value.set_renderer());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetRendererParam:
        Measure(value.set_renderer_param());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetRotation:
        Measure(value.set_rotation());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetScale:
        Measure(value.set_scale());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetShape:
        Measure(value.set_shape());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetSize:
        Measure(value.set_size());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetStereoCameraProjection:
        Measure(value.set_stereo_camera_projection());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetTag:
        Measure(value.set_tag());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetTexture:
        Measure(value.set_texture());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetTranslation:
        Measure(value.set_translation());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetViewHolderBoundsColor:
        Measure(value.set_view_holder_bounds_color());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kSetViewProperties:
        Measure(value.set_view_properties());
        break;
      case ::fuchsia::ui::gfx::Command::Tag::kTakeSnapshotCmd:
        Measure(value.take_snapshot_cmd());
        break;
    }
  }

  void Measure(const ::fuchsia::ui::gfx::CompositorArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::CreateResourceCmd& value) {
    num_bytes_ += FIDL_ALIGN(32);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::CreateResourceCmd& value) {
    MeasureOutOfLine(value.resource);
  }

  void Measure(const ::fuchsia::ui::gfx::DetachChildrenCmd& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::DetachCmd& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::DetachLightCmd& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::DetachLightsCmd& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::DirectionalLightArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::DisplayCompositorArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::EntityNodeArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::ExportResourceCmdDeprecated& value) {
    num_bytes_ += FIDL_ALIGN(8);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::gfx::ExportResourceCmdDeprecated& value) {
    num_handles_ += 1;
  }

  void Measure(const ::fuchsia::ui::gfx::FactoredTransform& value) {
    num_bytes_ += FIDL_ALIGN(52);
  }

  void Measure(const ::fuchsia::ui::gfx::FloatValue& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::ImageArgs& value) {
    num_bytes_ += FIDL_ALIGN(40);
  }

  void Measure(const ::fuchsia::ui::gfx::ImagePipe2Args& value) {
    num_bytes_ += FIDL_ALIGN(4);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::gfx::ImagePipe2Args& value) {
    num_handles_ += 1;
  }

  void Measure(const ::fuchsia::ui::gfx::ImagePipeArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::gfx::ImagePipeArgs& value) {
    num_handles_ += 1;
  }

  void Measure(const ::fuchsia::ui::gfx::ImagePipeCompositorArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::gfx::ImagePipeCompositorArgs& value) {
    num_handles_ += 1;
  }

  void Measure(const ::fuchsia::ui::gfx::ImportResourceCmdDeprecated& value) {
    num_bytes_ += FIDL_ALIGN(12);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::gfx::ImportResourceCmdDeprecated& value) {
    num_handles_ += 1;
  }

  void Measure(const ::fuchsia::ui::gfx::LayerArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::LayerStackArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::MaterialArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::Matrix4Value& value) {
    num_bytes_ += FIDL_ALIGN(68);
  }

  void Measure(const ::fuchsia::ui::gfx::MemoryArgs& value) {
    num_bytes_ += FIDL_ALIGN(24);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::gfx::MemoryArgs& value) {
    num_handles_ += 1;
  }

  void Measure(const ::fuchsia::ui::gfx::MeshArgs& value) {
    num_bytes_ += FIDL_ALIGN(1);
  }

  void Measure(const ::fuchsia::ui::gfx::MeshVertexFormat& value) {
    num_bytes_ += FIDL_ALIGN(12);
  }

  void Measure(const ::fuchsia::ui::gfx::OpacityNodeArgsHACK& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::PointLightArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::Quaternion& value) {
    num_bytes_ += FIDL_ALIGN(16);
  }

  void Measure(const ::fuchsia::ui::gfx::QuaternionValue& value) {
    num_bytes_ += FIDL_ALIGN(20);
  }

  void Measure(const ::fuchsia::ui::gfx::RectangleArgs& value) {
    num_bytes_ += FIDL_ALIGN(48);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::RectangleArgs& value) {
    MeasureOutOfLine(value.width);
    MeasureOutOfLine(value.height);
  }

  void Measure(const ::fuchsia::ui::gfx::ReleaseResourceCmd& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::RemoveAllLayersCmd& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::RemoveLayerCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::RendererArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::RendererParam& value) {
    num_bytes_ += 24;
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::RendererParam& value) {
    switch (value.Which()) {
      case ::fuchsia::ui::gfx::RendererParam::Tag::Invalid:
        MaxOut();
        break;
      case ::fuchsia::ui::gfx::RendererParam::Tag::kEnableDebugging:
        num_bytes_ += 8;
        break;
      case ::fuchsia::ui::gfx::RendererParam::Tag::kReserved:
        num_bytes_ += 8;
        break;
      case ::fuchsia::ui::gfx::RendererParam::Tag::kShadowTechnique:
        num_bytes_ += 8;
        break;
    }
  }

  void Measure(const ::fuchsia::ui::gfx::ResourceArgs& value) {
    num_bytes_ += 24;
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::ResourceArgs& value) {
    switch (value.Which()) {
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::Invalid:
        MaxOut();
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kAmbientLight:
        Measure(value.ambient_light());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kBuffer:
        Measure(value.buffer());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kCamera:
        Measure(value.camera());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kCircle:
        Measure(value.circle());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kClipNode:
        Measure(value.clip_node());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kCompositor:
        Measure(value.compositor());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kDirectionalLight:
        Measure(value.directional_light());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kDisplayCompositor:
        Measure(value.display_compositor());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kEntityNode:
        Measure(value.entity_node());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kImage:
        Measure(value.image());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kImagePipe:
        Measure(value.image_pipe());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kImagePipe2:
        Measure(value.image_pipe2());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kImagePipeCompositor:
        Measure(value.image_pipe_compositor());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kLayer:
        Measure(value.layer());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kLayerStack:
        Measure(value.layer_stack());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kMaterial:
        Measure(value.material());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kMemory:
        Measure(value.memory());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kMesh:
        Measure(value.mesh());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kOpacityNode:
        Measure(value.opacity_node());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kPointLight:
        Measure(value.point_light());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kRectangle:
        Measure(value.rectangle());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kRenderer:
        Measure(value.renderer());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kRoundedRectangle:
        Measure(value.rounded_rectangle());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kScene:
        Measure(value.scene());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kShapeNode:
        Measure(value.shape_node());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kStereoCamera:
        Measure(value.stereo_camera());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kVariable:
        Measure(value.variable());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kView:
        Measure(value.view());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kView3:
        Measure(value.view3());
        break;
      case ::fuchsia::ui::gfx::ResourceArgs::Tag::kViewHolder:
        Measure(value.view_holder());
        break;
    }
  }

  void Measure(const ::fuchsia::ui::gfx::RoundedRectangleArgs& value) {
    num_bytes_ += FIDL_ALIGN(144);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::RoundedRectangleArgs& value) {
    MeasureOutOfLine(value.width);
    MeasureOutOfLine(value.height);
    MeasureOutOfLine(value.top_left_radius);
    MeasureOutOfLine(value.top_right_radius);
    MeasureOutOfLine(value.bottom_right_radius);
    MeasureOutOfLine(value.bottom_left_radius);
  }

  void Measure(const ::fuchsia::ui::gfx::SceneAddAmbientLightCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SceneAddDirectionalLightCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SceneAddPointLightCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SceneArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::SendSizeChangeHintCmdHACK& value) {
    num_bytes_ += FIDL_ALIGN(12);
  }

  void Measure(const ::fuchsia::ui::gfx::SetAnchorCmd& value) {
    num_bytes_ += FIDL_ALIGN(20);
  }

  void Measure(const ::fuchsia::ui::gfx::SetCameraClipSpaceTransformCmd& value) {
    num_bytes_ += FIDL_ALIGN(16);
  }

  void Measure(const ::fuchsia::ui::gfx::SetCameraCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetCameraPoseBufferCmd& value) {
    num_bytes_ += FIDL_ALIGN(32);
  }

  void Measure(const ::fuchsia::ui::gfx::SetCameraProjectionCmd& value) {
    num_bytes_ += FIDL_ALIGN(12);
  }

  void Measure(const ::fuchsia::ui::gfx::SetCameraTransformCmd& value) {
    num_bytes_ += FIDL_ALIGN(52);
  }

  void Measure(const ::fuchsia::ui::gfx::SetClipCmd& value) {
    num_bytes_ += FIDL_ALIGN(12);
  }

  void Measure(const ::fuchsia::ui::gfx::SetClipPlanesCmd& value) {
    num_bytes_ += FIDL_ALIGN(24);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::SetClipPlanesCmd& value) {
    num_bytes_ += FIDL_ALIGN(value.clip_planes.size() * 16);
  }

  void Measure(const ::fuchsia::ui::gfx::SetColorCmd& value) {
    num_bytes_ += FIDL_ALIGN(12);
  }

  void Measure(const ::fuchsia::ui::gfx::SetDisableClippingCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK& value) {
    num_bytes_ += FIDL_ALIGN(64);
  }

  void Measure(const ::fuchsia::ui::gfx::SetDisplayRotationCmdHACK& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetEnableDebugViewBoundsCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetEventMaskCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetHitTestBehaviorCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetImportFocusCmdDEPRECATED& value) {
    num_bytes_ += FIDL_ALIGN(1);
  }

  void Measure(const ::fuchsia::ui::gfx::SetLabelCmd& value) {
    num_bytes_ += FIDL_ALIGN(24);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::SetLabelCmd& value) {
    num_bytes_ += FIDL_ALIGN(value.label.length());
  }

  void Measure(const ::fuchsia::ui::gfx::SetLayerStackCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetLightColorCmd& value) {
    num_bytes_ += FIDL_ALIGN(20);
  }

  void Measure(const ::fuchsia::ui::gfx::SetLightDirectionCmd& value) {
    num_bytes_ += FIDL_ALIGN(20);
  }

  void Measure(const ::fuchsia::ui::gfx::SetMaterialCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetOpacityCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetPointLightFalloffCmd& value) {
    num_bytes_ += FIDL_ALIGN(12);
  }

  void Measure(const ::fuchsia::ui::gfx::SetPointLightPositionCmd& value) {
    num_bytes_ += FIDL_ALIGN(20);
  }

  void Measure(const ::fuchsia::ui::gfx::SetRendererCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetRendererParamCmd& value) {
    num_bytes_ += FIDL_ALIGN(32);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::SetRendererParamCmd& value) {
    MeasureOutOfLine(value.param);
  }

  void Measure(const ::fuchsia::ui::gfx::SetRotationCmd& value) {
    num_bytes_ += FIDL_ALIGN(24);
  }

  void Measure(const ::fuchsia::ui::gfx::SetScaleCmd& value) {
    num_bytes_ += FIDL_ALIGN(20);
  }

  void Measure(const ::fuchsia::ui::gfx::SetShapeCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetSizeCmd& value) {
    num_bytes_ += FIDL_ALIGN(16);
  }

  void Measure(const ::fuchsia::ui::gfx::SetStereoCameraProjectionCmd& value) {
    num_bytes_ += FIDL_ALIGN(140);
  }

  void Measure(const ::fuchsia::ui::gfx::SetTagCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetTextureCmd& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::SetTranslationCmd& value) {
    num_bytes_ += FIDL_ALIGN(20);
  }

  void Measure(const ::fuchsia::ui::gfx::SetViewHolderBoundsColorCmd& value) {
    num_bytes_ += FIDL_ALIGN(20);
  }

  void Measure(const ::fuchsia::ui::gfx::SetViewPropertiesCmd& value) {
    num_bytes_ += FIDL_ALIGN(56);
  }

  void Measure(const ::fuchsia::ui::gfx::ShapeNodeArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::StereoCameraArgs& value) {
    num_bytes_ += FIDL_ALIGN(4);
  }

  void Measure(const ::fuchsia::ui::gfx::TakeSnapshotCmdDEPRECATED& value) {
    num_bytes_ += FIDL_ALIGN(8);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::gfx::TakeSnapshotCmdDEPRECATED& value) {
    num_handles_ += 1;
  }

  void Measure(const ::fuchsia::ui::gfx::Value& value) {
    num_bytes_ += 24;
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::Value& value) {
    switch (value.Which()) {
      case ::fuchsia::ui::gfx::Value::Tag::Invalid:
        MaxOut();
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kColorRgb:
        Measure(value.color_rgb());
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kColorRgba:
        Measure(value.color_rgba());
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kDegrees:
        num_bytes_ += 8;
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kMatrix4x4:
        Measure(value.matrix4x4());
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kQuaternion:
        Measure(value.quaternion());
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kTransform:
        Measure(value.transform());
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kVariableId:
        num_bytes_ += 8;
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kVector1:
        num_bytes_ += 8;
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kVector2:
        Measure(value.vector2());
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kVector3:
        Measure(value.vector3());
        break;
      case ::fuchsia::ui::gfx::Value::Tag::kVector4:
        Measure(value.vector4());
        break;
    }
  }

  void Measure(const ::fuchsia::ui::gfx::VariableArgs& value) {
    num_bytes_ += FIDL_ALIGN(32);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::VariableArgs& value) {
    MeasureOutOfLine(value.initial_value);
  }

  void Measure(const ::fuchsia::ui::gfx::Vector2Value& value) {
    num_bytes_ += FIDL_ALIGN(12);
  }

  void Measure(const ::fuchsia::ui::gfx::Vector3Value& value) {
    num_bytes_ += FIDL_ALIGN(16);
  }

  void Measure(const ::fuchsia::ui::gfx::ViewArgs& value) {
    num_bytes_ += FIDL_ALIGN(24);
    MeasureHandles(value);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::ViewArgs& value) {
    if (value.debug_name) {
      num_bytes_ += FIDL_ALIGN(value.debug_name->length());
    }
  }

  void MeasureHandles(const ::fuchsia::ui::gfx::ViewArgs& value) {
    MeasureHandles(value.token);
  }

  void Measure(const ::fuchsia::ui::gfx::ViewArgs3& value) {
    num_bytes_ += FIDL_ALIGN(32);
    MeasureHandles(value);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::ViewArgs3& value) {
    if (value.debug_name) {
      num_bytes_ += FIDL_ALIGN(value.debug_name->length());
    }
  }

  void MeasureHandles(const ::fuchsia::ui::gfx::ViewArgs3& value) {
    MeasureHandles(value.token);
    MeasureHandles(value.control_ref);
    MeasureHandles(value.view_ref);
  }

  void Measure(const ::fuchsia::ui::gfx::ViewHolderArgs& value) {
    num_bytes_ += FIDL_ALIGN(24);
    MeasureHandles(value);
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::gfx::ViewHolderArgs& value) {
    if (value.debug_name) {
      num_bytes_ += FIDL_ALIGN(value.debug_name->length());
    }
  }

  void MeasureHandles(const ::fuchsia::ui::gfx::ViewHolderArgs& value) {
    MeasureHandles(value.token);
  }

  void Measure(const ::fuchsia::ui::gfx::ViewProperties& value) {
    num_bytes_ += FIDL_ALIGN(52);
  }

  void Measure(const ::fuchsia::ui::gfx::mat4& value) {
    num_bytes_ += FIDL_ALIGN(64);
  }

  void Measure(const ::fuchsia::ui::gfx::vec2& value) {
    num_bytes_ += FIDL_ALIGN(8);
  }

  void Measure(const ::fuchsia::ui::gfx::vec3& value) {
    num_bytes_ += FIDL_ALIGN(12);
  }

  void Measure(const ::fuchsia::ui::gfx::vec4& value) {
    num_bytes_ += FIDL_ALIGN(16);
  }

  void Measure(const ::fuchsia::ui::input::Command& value) {
    num_bytes_ += 24;
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::input::Command& value) {
    switch (value.Which()) {
      case ::fuchsia::ui::input::Command::Tag::Invalid:
        MaxOut();
        break;
      case ::fuchsia::ui::input::Command::Tag::kSendKeyboardInput:
        Measure(value.send_keyboard_input());
        break;
      case ::fuchsia::ui::input::Command::Tag::kSendPointerInput:
        Measure(value.send_pointer_input());
        break;
      case ::fuchsia::ui::input::Command::Tag::kSetHardKeyboardDelivery:
        Measure(value.set_hard_keyboard_delivery());
        break;
      case ::fuchsia::ui::input::Command::Tag::kSetParallelDispatch:
        Measure(value.set_parallel_dispatch());
        break;
    }
  }

  void Measure(const ::fuchsia::ui::input::KeyboardEvent& value) {
    num_bytes_ += FIDL_ALIGN(32);
  }

  void Measure(const ::fuchsia::ui::input::PointerEvent& value) {
    num_bytes_ += FIDL_ALIGN(48);
  }

  void Measure(const ::fuchsia::ui::input::SendKeyboardInputCmd& value) {
    num_bytes_ += FIDL_ALIGN(40);
  }

  void Measure(const ::fuchsia::ui::input::SendPointerInputCmd& value) {
    num_bytes_ += FIDL_ALIGN(56);
  }

  void Measure(const ::fuchsia::ui::input::SetHardKeyboardDeliveryCmd& value) {
    num_bytes_ += FIDL_ALIGN(1);
  }

  void Measure(const ::fuchsia::ui::input::SetParallelDispatchCmd& value) {
    num_bytes_ += FIDL_ALIGN(1);
  }

  void Measure(const ::fuchsia::ui::scenic::Command& value) {
    num_bytes_ += 24;
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::scenic::Command& value) {
    switch (value.Which()) {
      case ::fuchsia::ui::scenic::Command::Tag::Invalid:
        MaxOut();
        break;
      case ::fuchsia::ui::scenic::Command::Tag::kGfx:
        Measure(value.gfx());
        break;
      case ::fuchsia::ui::scenic::Command::Tag::kInput:
        Measure(value.input());
        break;
      case ::fuchsia::ui::scenic::Command::Tag::kViews:
        Measure(value.views());
        break;
    }
  }

  void Measure(const ::fuchsia::ui::views::Command& value) {
    num_bytes_ += 24;
    MeasureOutOfLine(value);
  }

  void MeasureOutOfLine(const ::fuchsia::ui::views::Command& value) {
    switch (value.Which()) {
      case ::fuchsia::ui::views::Command::Tag::Invalid:
        MaxOut();
        break;
      case ::fuchsia::ui::views::Command::Tag::kEmpty:
        num_bytes_ += 8;
        break;
    }
  }

  void Measure(const ::fuchsia::ui::views::ViewHolderToken& value) {
    num_bytes_ += FIDL_ALIGN(4);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::views::ViewHolderToken& value) {
    num_handles_ += 1;
  }

  void Measure(const ::fuchsia::ui::views::ViewRef& value) {
    num_bytes_ += FIDL_ALIGN(4);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::views::ViewRef& value) {
    num_handles_ += 1;
  }

  void Measure(const ::fuchsia::ui::views::ViewRefControl& value) {
    num_bytes_ += FIDL_ALIGN(4);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::views::ViewRefControl& value) {
    num_handles_ += 1;
  }

  void Measure(const ::fuchsia::ui::views::ViewToken& value) {
    num_bytes_ += FIDL_ALIGN(4);
    MeasureHandles(value);
  }

  void MeasureHandles(const ::fuchsia::ui::views::ViewToken& value) {
    num_handles_ += 1;
  }

  Size Done() {
    if (maxed_out_) {
      return Size(ZX_CHANNEL_MAX_MSG_BYTES, ZX_CHANNEL_MAX_MSG_HANDLES);
    }
    return Size(num_bytes_, num_handles_);
  }

private:
  void MaxOut() { maxed_out_ = true; }

  bool maxed_out_ = false;
  int64_t num_bytes_ = 0;
  int64_t num_handles_ = 0;
};

}  // namespace

Size Measure(const ::fuchsia::ui::scenic::Command& value) {
  MeasuringTape tape;
  tape.Measure(value);
  return tape.Done();
}


}  // scenic
}  // ui
}  // fuchsia
}  // measure_tape
