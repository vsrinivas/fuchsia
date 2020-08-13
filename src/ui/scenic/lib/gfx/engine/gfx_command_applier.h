// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_GFX_COMMAND_APPLIER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_GFX_COMMAND_APPLIER_H_

#include <fuchsia/ui/views/cpp/fidl.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/flib/fence_set_listener.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/scenic/lib/gfx/engine/resource_map.h"
#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"
#include "src/ui/scenic/lib/gfx/engine/session_context.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"
#include "src/ui/scenic/lib/gfx/resources/resource_context.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scenic_impl {

namespace display {
class DisplayManager;
}

namespace gfx {

class Image;
using ImagePtr = fxl::RefPtr<Image>;

class ImageBase;
using ImageBasePtr = fxl::RefPtr<ImageBase>;

class ImagePipe;
using ImagePipePtr = fxl::RefPtr<ImagePipe>;

class Session;
class Sysmem;

// Callback used for pipeline warmup.
using WarmPipelineCacheCallback = fit::function<void(vk::Format framebuffer_format)>;

// Graphical context for a set of session updates.
// The CommandContext is only valid during a single processing batch, and should
// not be accessed/stored outside of that.
struct CommandContext {
  Sysmem* sysmem = nullptr;
  display::DisplayManager* display_manager = nullptr;
  WarmPipelineCacheCallback warm_pipeline_cache_callback;
  fxl::WeakPtr<SceneGraph> scene_graph;
};

// Responsible for applying gfx commands to sessions.
// Does not own any state. The session to be modified is instead
// passed in as an argument to ApplyCommand.
class GfxCommandApplier {
 public:
  // Apply the operation to the current session state.  Return true if
  // successful, and false if the op is somehow invalid.  In the latter case,
  // the Session is left unchanged.
  static bool ApplyCommand(Session* session, CommandContext* command_context,
                           fuchsia::ui::gfx::Command command);

 private:
  // Return false and logs an error to the session's ErrorReporter if the value
  // is not of the expected type.
  // NOTE: although failure does not halt execution of the program,
  // it does indicate client error, and will be used by the caller
  // to tear down the Session.
  static bool AssertValueIsOfType(const fuchsia::ui::gfx::Value& value,
                                  const fuchsia::ui::gfx::Value::Tag* tags, size_t tag_count,
                                  Session* session);
  template <size_t N>
  static bool AssertValueIsOfType(const fuchsia::ui::gfx::Value& value,
                                  const std::array<fuchsia::ui::gfx::Value::Tag, N>& tags,
                                  Session* session) {
    return AssertValueIsOfType(value, tags.data(), N, session);
  }

  // Functions for applying specific Commands. Called by ApplyCommand().
  static bool ApplyCreateResourceCmd(Session* session, CommandContext* command_context,
                                     fuchsia::ui::gfx::CreateResourceCmd command);
  static bool ApplyReleaseResourceCmd(Session* session,
                                      fuchsia::ui::gfx::ReleaseResourceCmd command);
  static bool ApplyExportResourceCmd(Session* session,
                                     fuchsia::ui::gfx::ExportResourceCmdDeprecated command);
  static bool ApplyImportResourceCmd(Session* session,
                                     fuchsia::ui::gfx::ImportResourceCmdDeprecated command);
  static bool ApplyAddChildCmd(Session* session, fuchsia::ui::gfx::AddChildCmd command);
  static bool ApplyAddPartCmd(Session* session, fuchsia::ui::gfx::AddPartCmd command);
  static bool ApplyDetachCmd(Session* session, fuchsia::ui::gfx::DetachCmd command);
  static bool ApplyDetachChildrenCmd(Session* session, fuchsia::ui::gfx::DetachChildrenCmd command);
  static bool ApplySetTagCmd(Session* session, fuchsia::ui::gfx::SetTagCmd command);
  static bool ApplySetTranslationCmd(Session* session, fuchsia::ui::gfx::SetTranslationCmd command);
  static bool ApplySetScaleCmd(Session* session, fuchsia::ui::gfx::SetScaleCmd command);
  static bool ApplySetRotationCmd(Session* session, fuchsia::ui::gfx::SetRotationCmd command);
  static bool ApplySetAnchorCmd(Session* session, fuchsia::ui::gfx::SetAnchorCmd command);
  static bool ApplySetSizeCmd(Session* session, fuchsia::ui::gfx::SetSizeCmd command);
  static bool ApplySetOpacityCmd(Session* session, fuchsia::ui::gfx::SetOpacityCmd command);
  static bool ApplySendSizeChangeHintCmd(Session* session,
                                         fuchsia::ui::gfx::SendSizeChangeHintCmdHACK command);
  static bool ApplySetShapeCmd(Session* session, fuchsia::ui::gfx::SetShapeCmd command);
  static bool ApplySetMaterialCmd(Session* session, fuchsia::ui::gfx::SetMaterialCmd command);
  static bool ApplySetClipCmd(Session* session, fuchsia::ui::gfx::SetClipCmd command);
  static bool ApplySetClipPlanesCmd(Session* session, fuchsia::ui::gfx::SetClipPlanesCmd command);
  static bool ApplySetViewPropertiesCmd(Session* session,
                                        fuchsia::ui::gfx::SetViewPropertiesCmd command);
  static bool ApplySetHitTestBehaviorCmd(Session* session,
                                         fuchsia::ui::gfx::SetHitTestBehaviorCmd command);
  static bool ApplySetCameraCmd(Session* session, fuchsia::ui::gfx::SetCameraCmd command);
  static bool ApplySetCameraTransformCmd(Session* session,
                                         fuchsia::ui::gfx::SetCameraTransformCmd command);
  static bool ApplySetCameraProjectionCmd(Session* session,
                                          fuchsia::ui::gfx::SetCameraProjectionCmd command);
  static bool ApplySetStereoCameraProjectionCmd(
      Session* session, fuchsia::ui::gfx::SetStereoCameraProjectionCmd command);
  static bool ApplySetCameraClipSpaceTransformCmd(
      Session* session, fuchsia::ui::gfx::SetCameraClipSpaceTransformCmd command);
  static bool ApplySetCameraPoseBufferCmd(Session* session,
                                          fuchsia::ui::gfx::SetCameraPoseBufferCmd command);
  static bool ApplySetLightColorCmd(Session* session, fuchsia::ui::gfx::SetLightColorCmd command);
  static bool ApplySetLightDirectionCmd(Session* session,
                                        fuchsia::ui::gfx::SetLightDirectionCmd command);
  static bool ApplySetPointLightPositionCmd(Session* session,
                                            fuchsia::ui::gfx::SetPointLightPositionCmd command);
  static bool ApplySetPointLightFalloffCmd(Session* session,
                                           fuchsia::ui::gfx::SetPointLightFalloffCmd command);
  static bool ApplyAddLightCmd(Session* session, fuchsia::ui::gfx::AddLightCmd command);
  static bool ApplySceneAddAmbientLightCmd(Session* session,
                                           fuchsia::ui::gfx::SceneAddAmbientLightCmd command);
  static bool ApplySceneAddDirectionalLightCmd(
      Session* session, fuchsia::ui::gfx::SceneAddDirectionalLightCmd command);
  static bool ApplySceneAddPointLightCmd(Session* session,
                                         fuchsia::ui::gfx::SceneAddPointLightCmd command);
  static bool ApplyDetachLightCmd(Session* session, fuchsia::ui::gfx::DetachLightCmd command);
  static bool ApplyDetachLightsCmd(Session* session, fuchsia::ui::gfx::DetachLightsCmd command);
  static bool ApplySetTextureCmd(Session* session, fuchsia::ui::gfx::SetTextureCmd command);
  static bool ApplySetColorCmd(Session* session, fuchsia::ui::gfx::SetColorCmd command);
  static bool ApplyBindMeshBuffersCmd(Session* session,
                                      fuchsia::ui::gfx::BindMeshBuffersCmd command);
  static bool ApplyAddLayerCmd(Session* session, fuchsia::ui::gfx::AddLayerCmd command);
  static bool ApplyRemoveLayerCmd(Session* session, fuchsia::ui::gfx::RemoveLayerCmd command);
  static bool ApplyRemoveAllLayersCmd(Session* session,
                                      fuchsia::ui::gfx::RemoveAllLayersCmd command);
  static bool ApplySetLayerStackCmd(Session* session, fuchsia::ui::gfx::SetLayerStackCmd command);
  static bool ApplySetRendererCmd(Session* session, fuchsia::ui::gfx::SetRendererCmd command);
  static bool ApplySetRendererParamCmd(Session* session,
                                       fuchsia::ui::gfx::SetRendererParamCmd command);
  static bool ApplySetEventMaskCmd(Session* session, fuchsia::ui::gfx::SetEventMaskCmd command);
  static bool ApplySetLabelCmd(Session* session, fuchsia::ui::gfx::SetLabelCmd command);
  static bool ApplySetDisableClippingCmd(Session* session,
                                         fuchsia::ui::gfx::SetDisableClippingCmd command);

  // Resource creation functions, called by ApplyCreateResourceCmd(Session*
  // session, ).
  static bool ApplyCreateMemory(Session* session, ResourceId id, fuchsia::ui::gfx::MemoryArgs args);
  static bool ApplyCreateImage(Session* session, ResourceId id, fuchsia::ui::gfx::ImageArgs args);
  static bool ApplyCreateImagePipe(Session* session, ResourceId id,
                                   fuchsia::ui::gfx::ImagePipeArgs args);
  static bool ApplyCreateImagePipe2(Session* session, ResourceId id,
                                    fuchsia::ui::gfx::ImagePipe2Args args);
  static bool ApplyCreateBuffer(Session* session, ResourceId id, fuchsia::ui::gfx::BufferArgs args);
  static bool ApplyCreateScene(Session* session, ResourceId id, fuchsia::ui::gfx::SceneArgs args);
  static bool ApplyCreateCamera(Session* session, ResourceId id, fuchsia::ui::gfx::CameraArgs args);
  static bool ApplyCreateStereoCamera(Session* session, ResourceId id,
                                      fuchsia::ui::gfx::StereoCameraArgs args);
  static bool ApplyCreateRenderer(Session* session, ResourceId id,
                                  fuchsia::ui::gfx::RendererArgs args);
  static bool ApplyCreateAmbientLight(Session* session, ResourceId id,
                                      fuchsia::ui::gfx::AmbientLightArgs args);
  static bool ApplyCreateDirectionalLight(Session* session, ResourceId id,
                                          fuchsia::ui::gfx::DirectionalLightArgs args);
  static bool ApplyCreatePointLight(Session* session, ResourceId id,
                                    fuchsia::ui::gfx::PointLightArgs args);
  static bool ApplyCreateRectangle(Session* session, ResourceId id,
                                   fuchsia::ui::gfx::RectangleArgs args);
  static bool ApplyCreateRoundedRectangle(Session* session, CommandContext* command_context,
                                          ResourceId id,
                                          fuchsia::ui::gfx::RoundedRectangleArgs args);
  static bool ApplyCreateCircle(Session* session, ResourceId id, fuchsia::ui::gfx::CircleArgs args);
  static bool ApplyCreateMesh(Session* session, ResourceId id, fuchsia::ui::gfx::MeshArgs args);
  static bool ApplyCreateMaterial(Session* session, ResourceId id,
                                  fuchsia::ui::gfx::MaterialArgs args);
  static bool ApplyCreateView(Session* session, ResourceId id, fuchsia::ui::gfx::ViewArgs args);
  static bool ApplyCreateViewHolder(Session* session, ResourceId id,
                                    fuchsia::ui::gfx::ViewHolderArgs args);
  static bool ApplyCreateView(Session* session, ResourceId id, fuchsia::ui::gfx::ViewArgs3 args);
  static bool ApplyCreateClipNode(Session* session, ResourceId id,
                                  fuchsia::ui::gfx::ClipNodeArgs args);
  static bool ApplyCreateEntityNode(Session* session, ResourceId id,
                                    fuchsia::ui::gfx::EntityNodeArgs args);
  static bool ApplyCreateOpacityNode(Session* session, ResourceId id,
                                     fuchsia::ui::gfx::OpacityNodeArgsHACK args);
  static bool ApplyCreateShapeNode(Session* session, ResourceId id,
                                   fuchsia::ui::gfx::ShapeNodeArgs args);
  static bool ApplyCreateCompositor(Session* session, ResourceId id,
                                    fuchsia::ui::gfx::CompositorArgs args);
  static bool ApplyCreateDisplayCompositor(Session* session, CommandContext* context, ResourceId id,
                                           fuchsia::ui::gfx::DisplayCompositorArgs args);
  static bool ApplyCreateImagePipeCompositor(Session* session, ResourceId id,
                                             fuchsia::ui::gfx::ImagePipeCompositorArgs args);
  static bool ApplyCreateLayerStack(Session* session, ResourceId id,
                                    fuchsia::ui::gfx::LayerStackArgs args);
  static bool ApplyCreateLayer(Session* session, ResourceId id, fuchsia::ui::gfx::LayerArgs args);
  static bool ApplyCreateVariable(Session* session, ResourceId id,
                                  fuchsia::ui::gfx::VariableArgs args);
  static bool ApplyTakeSnapshotCmdDEPRECATED(Session* session,
                                             fuchsia::ui::gfx::TakeSnapshotCmdDEPRECATED command);

  static bool ApplySetDisplayColorConversionCmd(
      Session* session, fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK command);

  static bool ApplySetDisplayMinimumRgbCmd(Session* session, CommandContext* command_context,
                                           fuchsia::ui::gfx::SetDisplayMinimumRgbCmdHACK command);

  static bool ApplySetDisplayRotationCmd(Session* session,
                                         fuchsia::ui::gfx::SetDisplayRotationCmdHACK command);

  static bool ApplySetEnableViewDebugBounds(Session* session,
                                            fuchsia::ui::gfx::SetEnableDebugViewBoundsCmd command);

  static bool ApplySetViewHolderBoundsColor(Session* sesion,
                                            fuchsia::ui::gfx::SetViewHolderBoundsColorCmd command);

  // Actually create resources.
  static ResourcePtr CreateMemory(Session* session, ResourceId id,
                                  fuchsia::ui::gfx::MemoryArgs args);
  static ResourcePtr CreateImage(Session* session, ResourceId id, MemoryPtr memory,
                                 fuchsia::ui::gfx::ImageArgs args);
  static ResourcePtr CreateBuffer(Session* session, ResourceId id, MemoryPtr memory,
                                  uint32_t memory_offset, uint32_t num_bytes);

  static ResourcePtr CreateScene(Session* session, ResourceId id, fuchsia::ui::gfx::SceneArgs args);
  static ResourcePtr CreateCamera(Session* session, ResourceId id,
                                  fuchsia::ui::gfx::CameraArgs args);
  static ResourcePtr CreateStereoCamera(Session* session, ResourceId id,
                                        fuchsia::ui::gfx::StereoCameraArgs args);
  static ResourcePtr CreateRenderer(Session* session, ResourceId id,
                                    fuchsia::ui::gfx::RendererArgs args);

  static ResourcePtr CreateAmbientLight(Session* session, ResourceId id);
  static ResourcePtr CreateDirectionalLight(Session* session, ResourceId id);
  static ResourcePtr CreatePointLight(Session* session, ResourceId id);

  static ResourcePtr CreateView(Session* session, ResourceId id, fuchsia::ui::gfx::ViewArgs args);
  static ResourcePtr CreateView(Session* session, ResourceId id, fuchsia::ui::gfx::ViewArgs3 args);
  static ResourcePtr CreateViewHolder(Session* session, ResourceId id,
                                      fuchsia::ui::gfx::ViewHolderArgs args);
  static ResourcePtr CreateClipNode(Session* session, ResourceId id,
                                    fuchsia::ui::gfx::ClipNodeArgs args);
  static ResourcePtr CreateEntityNode(Session* session, ResourceId id,
                                      fuchsia::ui::gfx::EntityNodeArgs args);
  static ResourcePtr CreateOpacityNode(Session* session, ResourceId id,
                                       fuchsia::ui::gfx::OpacityNodeArgsHACK args);
  static ResourcePtr CreateShapeNode(Session* session, ResourceId id,
                                     fuchsia::ui::gfx::ShapeNodeArgs args);

  static ResourcePtr CreateCompositor(Session* session, ResourceId id,
                                      fuchsia::ui::gfx::CompositorArgs args);
  static ResourcePtr CreateDisplayCompositor(Session* session, CommandContext* context,
                                             ResourceId id,
                                             fuchsia::ui::gfx::DisplayCompositorArgs args);
  static ResourcePtr CreateImagePipeCompositor(Session* session, ResourceId id,
                                               fuchsia::ui::gfx::ImagePipeCompositorArgs args);
  static ResourcePtr CreateLayerStack(Session* session, ResourceId id,
                                      fuchsia::ui::gfx::LayerStackArgs args);
  static ResourcePtr CreateLayer(Session* session, ResourceId id, fuchsia::ui::gfx::LayerArgs args);
  static ResourcePtr CreateCircle(Session* session, ResourceId id, float initial_radius);
  static ResourcePtr CreateRectangle(Session* session, ResourceId id, float width, float height);
  static ResourcePtr CreateRoundedRectangle(Session* session, CommandContext* command_context,
                                            ResourceId id, float width, float height,
                                            float top_left_radius, float top_right_radius,
                                            float bottom_right_radius, float bottom_left_radius);
  static ResourcePtr CreateMesh(Session* session, ResourceId id);
  static ResourcePtr CreateMaterial(Session* session, ResourceId id);
  static ResourcePtr CreateVariable(Session* session, ResourceId id,
                                    fuchsia::ui::gfx::VariableArgs args);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_GFX_COMMAND_APPLIER_H_
